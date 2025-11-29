/**
 * @file main.c
 * @brief ESP32 Modbus Water Meter with ThingsBoard Integration (Zephyr RTOS)
 * @version 2.0.0
 * @date 2025-11-29
 * @author AMR ALI
 *
 * @details
 * This firmware integrates real Modbus RTU water meter readings with cloud
 * connectivity. It reads data from a BOVE ultrasonic water meter via Modbus
 * and transmits telemetry to ThingsBoard cloud platform via MQTT over WiFi.
 *
 * Key Features:
 * - Modbus RTU communication (2400 baud, 8E1)
 * - Real-time meter data reading (flow, totals, pressure, temperature)
 * - WiFi connectivity with auto-reconnection
 * - MQTT communication with ThingsBoard
 * - Periodic telemetry transmission (every 30 seconds)
 * - Device attributes reporting
 * - CRC16 validation
 * - Error handling and logging
 *
 * Architecture:
 *   BOVE Meter <--Modbus RTU--> ESP32 <--WiFi--> Router <--Internet--> ThingsBoard
 *
 * Data Flow:
 *   1. WiFi Connection → 2. MQTT Connection → 3. Modbus Read
 *   4. Data Processing → 5. MQTT Publish → 6. Wait & Repeat
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/poll.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(water_meter, LOG_LEVEL_INF);

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* WiFi Configuration */
#define WIFI_SSID "!!Huawei"
#define WIFI_PSK "karamr195"

/* ThingsBoard Configuration */
#define THINGSBOARD_HOST "thingsboard.cloud"
#define THINGSBOARD_PORT 1883
#define ACCESS_TOKEN "JqkpupDR1nmXD6nbZX2S"
#define TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define ATTRIBUTES_TOPIC "v1/devices/me/attributes"

/* Modbus Configuration */
#define UART_DEVICE_NODE DT_NODELABEL(uart0)
#define MODBUS_SLAVE_ID 1
#define MODBUS_READ_INTERVAL_SEC 30

/* Buffer Sizes */
#define RX_BUFFER_SIZE 1024
#define TX_BUFFER_SIZE 1024
#define MODBUS_RX_BUFFER 256

/* ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================ */

/* UART Device */
static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static struct uart_config original_cfg;

/* MQTT Client */
static struct mqtt_client client;
static struct sockaddr_in broker_addr;
static uint8_t mqtt_rx_buffer[RX_BUFFER_SIZE];
static uint8_t mqtt_tx_buffer[TX_BUFFER_SIZE];

/* Network Management */
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_obtained, 0, 1);
static volatile bool mqtt_connected = false;

/* Meter Data */
typedef struct {
    uint32_t flow_rate;        // L/h × 100
    uint32_t forward_total;    // m³ × 1000
    uint32_t reverse_total;    // m³ × 1000
    uint16_t pressure;         // MPa × 1000
    uint16_t temperature;      // °C × 100
    uint16_t status;           // Status flags
    uint32_t serial_number;    // Serial number (BCD)
    uint8_t modbus_id;         // Modbus address
    uint16_t baud_code;        // Baud rate code
    bool valid;                // Data validity flag
} meter_data_t;

static meter_data_t meter_data = {0};

/* ============================================================================
 * MODBUS FUNCTIONS
 * ============================================================================ */

/**
 * @brief Calculate Modbus CRC16
 */
uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief Build Modbus read command (Function Code 0x03)
 */
void build_read_cmd(uint8_t *buf, uint8_t id)
{
    buf[0] = id;           // Slave address
    buf[1] = 0x03;         // Function code (Read Holding Registers)
    buf[2] = 0x00;         // Start address high
    buf[3] = 0x01;         // Start address low (register 1)
    buf[4] = 0x00;         // Quantity high
    buf[5] = 0x26;         // Quantity low (38 registers)
    
    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = crc & 0xFF;
    buf[7] = (crc >> 8) & 0xFF;
}

/**
 * @brief Read 32-bit value from Modbus response (big-endian word order)
 */
uint32_t read_u32(const uint8_t *d, int offset)
{
    uint32_t low = (d[offset] << 8) | d[offset + 1];
    uint32_t high = (d[offset + 2] << 8) | d[offset + 3];
    return (high << 16) | low;
}

/**
 * @brief Switch UART to Modbus mode (2400 baud, 8E1)
 */
void switch_to_modbus(void)
{
    struct uart_config modbus_cfg = {
        .baudrate = 2400,
        .parity = UART_CFG_PARITY_EVEN,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    uart_configure(uart_dev, &modbus_cfg);
}

/**
 * @brief Switch UART back to console mode
 */
void switch_to_console(void)
{
    uart_configure(uart_dev, &original_cfg);
    k_msleep(10);
}

/**
 * @brief Read data from water meter via Modbus RTU
 */
int read_meter_data(void)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[MODBUS_RX_BUFFER];
    int rx_len = 0;
    
    /* Switch to Modbus mode */
    switch_to_modbus();
    
    /* Build and send read command */
    build_read_cmd(tx_buf, MODBUS_SLAVE_ID);
    for (int i = 0; i < 8; i++) {
        uart_poll_out(uart_dev, tx_buf[i]);
    }
    
    /* Wait for response */
    k_msleep(100);
    
    /* Receive response with timeout */
    uint32_t start = k_uptime_get_32();
    uint32_t last_byte = start;
    
    while ((k_uptime_get_32() - start) < 2000) {
        uint8_t c;
        if (uart_poll_in(uart_dev, &c) == 0) {
            rx_buf[rx_len++] = c;
            last_byte = k_uptime_get_32();
            if (rx_len >= sizeof(rx_buf)) break;
        }
        
        /* Break if no data for 150ms */
        if (rx_len > 3 && (k_uptime_get_32() - last_byte) > 150) {
            break;
        }
        
        k_msleep(5);
    }
    
    /* Switch back to console */
    switch_to_console();
    
    /* Validate response */
    if (rx_len < 70) {
        LOG_WRN("Incomplete response (%d bytes)", rx_len);
        meter_data.valid = false;
        return -1;
    }
    
    /* Verify CRC */
    uint16_t recv_crc = rx_buf[rx_len-2] | (rx_buf[rx_len-1] << 8);
    uint16_t calc_crc = modbus_crc16(rx_buf, rx_len - 2);
    
    if (recv_crc != calc_crc) {
        LOG_ERR("CRC error (recv: 0x%04X, calc: 0x%04X)", recv_crc, calc_crc);
        meter_data.valid = false;
        return -1;
    }
    
    /* Verify slave ID and function code */
    if (rx_buf[0] != MODBUS_SLAVE_ID || rx_buf[1] != 0x03) {
        LOG_ERR("Invalid response header");
        meter_data.valid = false;
        return -1;
    }
    
    /* Parse meter data */
    const uint8_t *d = &rx_buf[3];
    
    meter_data.flow_rate = read_u32(d, 0);           // Register 1-2
    meter_data.forward_total = read_u32(d, 12);      // Register 7-8
    meter_data.reverse_total = read_u32(d, 18);      // Register 10-11
    meter_data.pressure = (d[36] << 8) | d[37];      // Register 19
    meter_data.temperature = (d[58] << 8) | d[59];   // Register 30
    meter_data.status = (d[38] << 8) | d[39];        // Register 20
    meter_data.serial_number = ((uint32_t)d[64] << 24) | ((uint32_t)d[65] << 16) | 
                               ((uint32_t)d[66] << 8) | d[67];  // Register 33-34
    meter_data.modbus_id = d[69];                    // Register 35
    meter_data.baud_code = (d[72] << 8) | d[73];     // Register 37
    meter_data.valid = true;
    
    LOG_INF("Meter data read successfully");
    return 0;
}

/* ============================================================================
 * WIFI FUNCTIONS
 * ============================================================================ */

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        int status = *((int *)cb->info);
        if (status == 0) {
            LOG_INF("WiFi connected");
            k_sem_give(&wifi_connected);
        } else {
            LOG_ERR("WiFi failed: %d", status);
        }
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_WRN("WiFi disconnected");
        mqtt_connected = false;
        k_sem_reset(&wifi_connected);
        k_sem_reset(&ipv4_obtained);
    }
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("IPv4 address obtained");
        k_sem_give(&ipv4_obtained);
    }
}

static int wifi_connect(void)
{
    struct net_if *iface;
    struct wifi_connect_req_params params;
    int retry = 0;
    const int max_retries = 10;
    
    LOG_INF("Initializing WiFi connection...");

    iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("No WiFi interface found");
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                NET_EVENT_WIFI_CONNECT_RESULT |
                                NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
                                NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

    memset(&params, 0, sizeof(params));
    params.ssid = WIFI_SSID;
    params.ssid_length = strlen(WIFI_SSID);
    params.psk = WIFI_PSK;
    params.psk_length = strlen(WIFI_PSK);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    /* Retry loop for WiFi connection */
    while (retry < max_retries) {
        LOG_INF("WiFi connection attempt %d/%d...", retry + 1, max_retries);
        
        /* Reset semaphores before retry */
        k_sem_reset(&wifi_connected);
        k_sem_reset(&ipv4_obtained);
        
        int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
        if (ret) {
            LOG_WRN("WiFi connection request failed: %d", ret);
            retry++;
            k_sleep(K_SECONDS(5));
            continue;
        }

        /* Wait for WiFi connection */
        if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
            LOG_WRN("WiFi connection timeout (attempt %d/%d)", retry + 1, max_retries);
            retry++;
            k_sleep(K_SECONDS(5));
            continue;
        }

        /* Wait for IPv4 address */
        if (k_sem_take(&ipv4_obtained, K_SECONDS(30)) != 0) {
            LOG_WRN("IPv4 acquisition timeout (attempt %d/%d)", retry + 1, max_retries);
            retry++;
            k_sleep(K_SECONDS(5));
            continue;
        }

        /* Success! */
        LOG_INF("WiFi connected successfully on attempt %d", retry + 1);
        return 0;
    }

    /* All retries failed */
    LOG_ERR("WiFi connection failed after %d attempts", max_retries);
    return -ETIMEDOUT;
}

/* ============================================================================
 * MQTT FUNCTIONS
 * ============================================================================ */

static int broker_init(void)
{
    struct zsock_addrinfo hints;
    struct zsock_addrinfo *result;

    LOG_INF("Resolving broker: %s", THINGSBOARD_HOST);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = zsock_getaddrinfo(THINGSBOARD_HOST, NULL, &hints, &result);
    if (ret != 0 || result == NULL) {
        LOG_ERR("DNS resolution failed");
        return -EINVAL;
    }

    memcpy(&broker_addr, result->ai_addr, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(THINGSBOARD_PORT);

    zsock_freeaddrinfo(result);
    LOG_INF("Broker resolved successfully");
    return 0;
}

static void mqtt_evt_handler(struct mqtt_client *const client,
                            const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT connection refused: %d", evt->result);
            mqtt_connected = false;
        } else {
            LOG_INF("MQTT connected");
            mqtt_connected = true;
        }
        break;
    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected");
        mqtt_connected = false;
        break;
    case MQTT_EVT_PUBACK:
        LOG_DBG("PUBACK received, msg_id: %d", evt->param.puback.message_id);
        break;
    default:
        break;
    }
}

static void prepare_mqtt_client(void)
{
    static char client_id[32];
    static struct mqtt_utf8 mqtt_user_name;

    snprintf(client_id, sizeof(client_id), "esp32_meter_%08x", 
             (unsigned int)sys_rand32_get());

    mqtt_client_init(&client);

    client.broker = (struct sockaddr *)&broker_addr;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (uint8_t *)client_id;
    client.client_id.size = strlen(client_id);
    client.user_name = &mqtt_user_name;
    mqtt_user_name.utf8 = (uint8_t *)ACCESS_TOKEN;
    mqtt_user_name.size = strlen(ACCESS_TOKEN);
    client.password = NULL;
    client.protocol_version = MQTT_VERSION_3_1_1;
    client.transport.type = MQTT_TRANSPORT_NON_SECURE;
    client.rx_buf = mqtt_rx_buffer;
    client.rx_buf_size = sizeof(mqtt_rx_buffer);
    client.tx_buf = mqtt_tx_buffer;
    client.tx_buf_size = sizeof(mqtt_tx_buffer);
    client.keepalive = 60;
}

static int thingsboard_connect(void)
{
    int ret;
    int retry = 0;

    LOG_INF("Connecting to ThingsBoard...");
    prepare_mqtt_client();

    while (retry < 5) {
        ret = mqtt_connect(&client);
        if (ret != 0) {
            LOG_ERR("mqtt_connect failed: %d", ret);
            goto cleanup;
        }

        struct zsock_pollfd fds[1];
        int timeout = 5000;

        fds[0].fd = client.transport.tcp.sock;
        fds[0].events = ZSOCK_POLLIN;

        while (timeout > 0) {
            int poll_ret = zsock_poll(fds, 1, 500);
            if (poll_ret > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
                mqtt_input(&client);
            }

            if (mqtt_connected) {
                LOG_INF("ThingsBoard connected successfully");
                return 0;
            }

            k_sleep(K_MSEC(500));
            timeout -= 500;
        }

cleanup:
        retry++;
        if (client.transport.tcp.sock >= 0) {
            struct mqtt_disconnect_param disc = {0};
            mqtt_disconnect(&client, &disc);
        }
        LOG_WRN("Connection attempt %d failed, retrying...", retry);
        k_sleep(K_SECONDS(2));
    }

    LOG_ERR("Failed to connect after 5 attempts");
    return -ECONNREFUSED;
}

static int send_telemetry(void)
{
    char payload[512];
    
    if (!mqtt_connected) {
        LOG_WRN("MQTT not connected, skipping telemetry");
        return -ENOTCONN;
    }
    
    if (!meter_data.valid) {
        LOG_WRN("Meter data invalid, skipping telemetry");
        return -EINVAL;
    }

    /* Build JSON payload with meter data (integer values only) */
    snprintf(payload, sizeof(payload),
             "{"
             "\"flowRate\":%u,"
             "\"forwardTotal\":%u,"
             "\"reverseTotal\":%u,"
             "\"pressure\":%u,"
             "\"temperature\":%u,"
             "\"status\":%u,"
             "\"leak\":%d,"
             "\"empty\":%d,"
             "\"lowBattery\":%d"
             "}",
             (unsigned int)meter_data.flow_rate,
             (unsigned int)meter_data.forward_total,
             (unsigned int)meter_data.reverse_total,
             (unsigned int)meter_data.pressure,
             (unsigned int)meter_data.temperature,
             (unsigned int)meter_data.status,
             (meter_data.status & 0x0004) ? 1 : 0,
             (meter_data.status & 0x0004) ? 1 : 0,
             (meter_data.status & 0x0020) ? 1 : 0
    );

    LOG_INF("Telemetry: %s", payload);

    struct mqtt_publish_param pub = {0};
    pub.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub.message.topic.topic.utf8 = (uint8_t *)TELEMETRY_TOPIC;
    pub.message.topic.topic.size = strlen(TELEMETRY_TOPIC);
    pub.message.payload.data = (uint8_t *)payload;
    pub.message.payload.len = strlen(payload);
    pub.message_id = sys_rand32_get();

    int rc = mqtt_publish(&client, &pub);
    if (rc) {
        LOG_ERR("MQTT publish failed: %d", rc);
    } else {
        LOG_INF("Telemetry published successfully");
    }
    return rc;
}

static int send_attributes(void)
{
    char payload[256];
    struct mqtt_publish_param pub = {0};
    const char *baud_str;

    if (!mqtt_connected) return -ENOTCONN;

    /* Determine baud rate string */
    switch(meter_data.baud_code) {
        case 0: baud_str = "9600"; break;
        case 1: baud_str = "2400"; break;
        case 2: baud_str = "4800"; break;
        case 3: baud_str = "1200"; break;
        default: baud_str = "unknown"; break;
    }

    snprintf(payload, sizeof(payload),
             "{"
             "\"firmwareVersion\":\"2.0.0\","
             "\"deviceModel\":\"BOVE-Modbus-Meter\","
             "\"serialNumber\":\"%08X\","
             "\"modbusId\":%u,"
             "\"baudRate\":\"%s\""
             "}",
             (unsigned int)meter_data.serial_number,
             (unsigned int)meter_data.modbus_id,
             baud_str
    );

    LOG_INF("Attributes: %s", payload);

    pub.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub.message.topic.topic.utf8 = (uint8_t *)ATTRIBUTES_TOPIC;
    pub.message.topic.topic.size = strlen(ATTRIBUTES_TOPIC);
    pub.message.payload.data = (uint8_t *)payload;
    pub.message.payload.len = strlen(payload);
    pub.message_id = sys_rand32_get();

    return mqtt_publish(&client, &pub);
}

static void mqtt_maintenance(void)
{
    if (mqtt_connected) {
        mqtt_input(&client);
        mqtt_live(&client);
    }
}

/* ============================================================================
 * MAIN APPLICATION
 * ============================================================================ */

int main(void)
{
    int ret;
    int wifi_retry_count = 0;
    const int max_wifi_retries = 3;
    
    LOG_INF("========================================");
    LOG_INF("  BOVE WATER METER IoT SYSTEM");
    LOG_INF("  Version: 2.0.0");
    LOG_INF("========================================");
    
    k_sleep(K_SECONDS(2));
    
    /* Initialize UART for Modbus */
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }
    
    uart_config_get(uart_dev, &original_cfg);
    LOG_INF("Console UART: %d baud", original_cfg.baudrate);
    
    /* Connect to WiFi with retries */
    while (wifi_retry_count < max_wifi_retries) {
        ret = wifi_connect();
        if (ret == 0) {
            break;  /* WiFi connected successfully */
        }
        
        wifi_retry_count++;
        if (wifi_retry_count < max_wifi_retries) {
            LOG_WRN("WiFi connection failed, waiting 10 seconds before retry %d/%d...", 
                    wifi_retry_count + 1, max_wifi_retries);
            k_sleep(K_SECONDS(10));
        }
    }
    
    if (ret != 0) {
        LOG_ERR("Failed to connect to WiFi after %d attempts", max_wifi_retries);
        LOG_INF("Continuing without WiFi connection - Modbus only mode");
        /* Continue without WiFi - can still read Modbus data */
    } else {
        k_sleep(K_SECONDS(1));
        
        /* Resolve broker address */
        ret = broker_init();
        if (ret != 0) {
            LOG_ERR("Broker initialization failed");
        } else {
            k_sleep(K_SECONDS(1));
            
            /* Connect to ThingsBoard */
            ret = thingsboard_connect();
            if (ret != 0) {
                LOG_ERR("ThingsBoard connection failed");
            }
        }
    }
    
    LOG_INF("========================================");
    LOG_INF("System operational - Starting data loop");
    LOG_INF("========================================");
    
    /* Main loop */
    int loop_count = 0;
    while (1) {
        loop_count++;
        
        /* Check WiFi connection status periodically */
        if (loop_count % 10 == 0) {  /* Every 10 iterations */
            if (!mqtt_connected) {
                LOG_WRN("MQTT disconnected, attempting reconnection...");
                
                /* Try to reconnect WiFi if needed */
                ret = wifi_connect();
                if (ret == 0) {
                    k_sleep(K_SECONDS(1));
                    ret = broker_init();
                    if (ret == 0) {
                        k_sleep(K_SECONDS(1));
                        thingsboard_connect();
                    }
                }
            }
        }
        
        /* Read meter data via Modbus */
        LOG_INF("Reading meter data...");
        ret = read_meter_data();
        
        if (ret == 0 && meter_data.valid) {
            /* Print meter data to console */
            LOG_INF("========================================");
            LOG_INF("METER DATA:");
            LOG_INF("  Flow Rate   : %u.%02u L/h", 
                    meter_data.flow_rate / 100, meter_data.flow_rate % 100);
            LOG_INF("  Forward Flow: %u.%03u m³", 
                    meter_data.forward_total / 1000, meter_data.forward_total % 1000);
            LOG_INF("  Reverse Flow: %u.%03u m³", 
                    meter_data.reverse_total / 1000, meter_data.reverse_total % 1000);
            LOG_INF("  Pressure    : %u.%03u MPa", 
                    meter_data.pressure / 1000, meter_data.pressure % 1000);
            LOG_INF("  Temperature : %u.%02u °C", 
                    meter_data.temperature / 100, meter_data.temperature % 100);
            LOG_INF("  Status      : 0x%04X %s", meter_data.status,
                    (meter_data.status == 0) ? "(Normal)" :
                    (meter_data.status & 0x0004) ? "(Empty!)" :
                    (meter_data.status & 0x0020) ? "(Low Battery!)" : "");
            LOG_INF("========================================");
            
            /* Send attributes on first successful read */
            static bool attrs_sent = false;
            if (!attrs_sent && mqtt_connected) {
                send_attributes();
                attrs_sent = true;
            }
            
            /* Send telemetry to ThingsBoard if connected */
            if (mqtt_connected) {
                if (send_telemetry() != 0) {
                    LOG_WRN("Telemetry transmission failed, will retry on next cycle");
                }
            } else {
                LOG_INF("MQTT not connected - data logged locally only");
            }
        } else {
            LOG_ERR("Failed to read meter data");
        }
        
        /* MQTT maintenance */
        if (mqtt_connected) {
            mqtt_maintenance();
        }
        
        /* Wait before next reading */
        LOG_INF("Waiting %d seconds...\n", MODBUS_READ_INTERVAL_SEC);
        k_sleep(K_SECONDS(MODBUS_READ_INTERVAL_SEC));
    }
    
    return 0;
}
