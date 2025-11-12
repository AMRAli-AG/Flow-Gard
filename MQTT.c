/*
 * ESP32 to ThingsBoard with Zephyr OS
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
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
#include <zephyr/sys/util.h>  /* For CLAMP macro */
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(thingsboard, LOG_LEVEL_INF);

/* WiFi Settings */
#define WIFI_SSID "!!Huawei"
#define WIFI_PSK "karamr195"

/* ThingsBoard Settings */
#define THINGSBOARD_HOST "thingsboard.cloud"
#define THINGSBOARD_PORT 1883
#define ACCESS_TOKEN "JqkpupDR1nmXD6nbZX2S"

/* MQTT Topics */
#define TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define ATTRIBUTES_TOPIC "v1/devices/me/attributes"

/* Buffer sizes */
#define RX_BUFFER_SIZE 1024
#define TX_BUFFER_SIZE 1024

/* MQTT Variables */
static struct mqtt_client client;
static struct sockaddr_in broker_addr;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];

/* WiFi Variables */
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_obtained, 0, 1);

/* Connection Status */
static volatile bool mqtt_connected = false;

/* WiFi Event Handler */
static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        int status = *((int *)cb->info);
        if (status == 0) {
            LOG_INF("WiFi connected successfully");
            k_sem_give(&wifi_connected);
        } else {
            LOG_ERR("WiFi connection failed: %d", status);
        }
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_WRN("WiFi disconnected");
        mqtt_connected = false;
        k_sem_reset(&wifi_connected);
        k_sem_reset(&ipv4_obtained);
    }
}

/* IPv4 Event Handler */
static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        struct net_if_addr *if_addr = (struct net_if_addr *)cb->info;
        char buf[NET_IPV4_ADDR_LEN];
        
        net_addr_ntop(AF_INET, &if_addr->address.in_addr, buf, sizeof(buf));
        LOG_INF("IPv4 Address obtained: %s", buf);
        k_sem_give(&ipv4_obtained);
    }
}

/* Connect to WiFi */
static int wifi_connect(void)
{
    struct net_if *iface;
    struct wifi_connect_req_params params;

    LOG_INF("===================================");
    LOG_INF("   Starting WiFi Connection");
    LOG_INF("===================================");

    iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("No WiFi interface found");
        return -ENODEV;
    }

    /* Register event callbacks */
    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                NET_EVENT_WIFI_CONNECT_RESULT |
                                NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
                                NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

    /* Configure WiFi parameters */
    memset(&params, 0, sizeof(params));
    params.ssid = WIFI_SSID;
    params.ssid_length = strlen(WIFI_SSID);
    params.psk = WIFI_PSK;
    params.psk_length = strlen(WIFI_PSK);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;

    LOG_INF("Connecting to WiFi: %s", WIFI_SSID);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
                      &params, sizeof(params));
    if (ret) {
        LOG_ERR("WiFi connect request failed: %d", ret);
        return ret;
    }

    /* Wait for WiFi connect */
    if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
        LOG_ERR("Timeout waiting for WiFi connect");
        return -ETIMEDOUT;
    }

    /* Wait for IP address */
    if (k_sem_take(&ipv4_obtained, K_SECONDS(30)) != 0) {
        LOG_ERR("Timeout waiting for IP address");
        return -ETIMEDOUT;
    }

    LOG_INF("WiFi connection established successfully");
    return 0;
}

/* Initialize broker address */
static int broker_init(void)
{
    struct zsock_addrinfo hints;
    struct zsock_addrinfo *result;
    int ret;

    LOG_INF("Resolving ThingsBoard address: %s", THINGSBOARD_HOST);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = zsock_getaddrinfo(THINGSBOARD_HOST, NULL, &hints, &result);
    if (ret != 0) {
        LOG_ERR("DNS resolution failed: %d", ret);
        return -EINVAL;
    }

    if (result == NULL) {
        LOG_ERR("No address found for ThingsBoard");
        return -EINVAL;
    }

    memcpy(&broker_addr, result->ai_addr, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(THINGSBOARD_PORT);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &broker_addr.sin_addr, ip_str, sizeof(ip_str));
    LOG_INF("ThingsBoard resolved to: %s:%d", ip_str, THINGSBOARD_PORT);

    zsock_freeaddrinfo(result);
    return 0;
}

/* MQTT Event Handler */
static void mqtt_evt_handler(struct mqtt_client *const client,
                            const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT connection refused: %d", evt->result);
            mqtt_connected = false;
        } else {
            LOG_INF("MQTT connected successfully to ThingsBoard!");
            mqtt_connected = true;
        }
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected");
        mqtt_connected = false;
        break;

    case MQTT_EVT_PUBACK:
        LOG_DBG("Message published successfully (PUBACK)");
        break;

    default:
        break;
    }
}

/* Prepare MQTT Client */
static void prepare_mqtt_client(void)
{
    static char client_id[32];
    static struct mqtt_utf8 mqtt_user_name;

    /* Generate unique client ID */
    snprintf(client_id, sizeof(client_id), "esp32_%08x", 
             (unsigned int)sys_rand32_get());

    /* Initialize MQTT client */
    mqtt_client_init(&client);

    /* Configure MQTT client */
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

    /* Configure buffers */
    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);
    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);

    /* Configure keepalive */
    client.keepalive = 60;

    LOG_INF("MQTT client configured with ID: %s", client_id);
}

/* Connect to ThingsBoard */
static int thingsboard_connect(void)
{
    int ret;
    int retry_count = 0;
    const int max_retries = 5;

    LOG_INF("===================================");
    LOG_INF("   Connecting to ThingsBoard");
    LOG_INF("===================================");

    prepare_mqtt_client();

    while (retry_count < max_retries) {
        LOG_INF("Connection attempt %d/%d", retry_count + 1, max_retries);

        ret = mqtt_connect(&client);
        if (ret != 0) {
            LOG_ERR("mqtt_connect failed: %d", ret);
            goto cleanup;
        }

        LOG_INF("MQTT connect initiated, waiting for CONNACK...");

        /* Wait for CONNACK with poll and input */
        struct zsock_pollfd fds[1];
        int timeout_ms = 5000;  /* 5 seconds total timeout */

        fds[0].fd = client.transport.tcp.sock;
        fds[0].events = ZSOCK_POLLIN;

        while (timeout_ms > 0) {
            int poll_ret = zsock_poll(fds, 1, 500);
            if (poll_ret < 0) {
                LOG_ERR("poll failed: %d", poll_ret);
                break;
            }

            if (poll_ret > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
                ret = mqtt_input(&client);
                if (ret != 0) {
                    LOG_ERR("mqtt_input failed: %d", ret);
                    break;
                }
            }

            if (mqtt_connected) {
                LOG_INF("Successfully connected to ThingsBoard!");
                return 0;
            }

            k_sleep(K_MSEC(500));
            timeout_ms -= 500;
        }

        LOG_WRN("CONNACK not received, retrying...");

cleanup:
        retry_count++;

        /* Cleanup before retry */
        if (client.transport.tcp.sock >= 0) {
            struct mqtt_disconnect_param disc_param = {0};
            mqtt_disconnect(&client, &disc_param);
        }

        k_sleep(K_SECONDS(2));
    }

    LOG_ERR("Failed to connect to ThingsBoard after %d attempts", max_retries);
    return -ECONNREFUSED;
}

/* Send telemetry data */
static int send_telemetry(float temperature, float humidity)
{
    char payload[128];
    struct mqtt_publish_param pub_param;
    int ret;

    if (!mqtt_connected) {
        LOG_WRN("MQTT not connected, cannot send telemetry");
        return -ENOTCONN;
    }

    /* Prepare payload */
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f}",
             (double)temperature, (double)humidity);

    LOG_INF("Sending telemetry: %s", payload);

    /* Configure publish parameters */
    memset(&pub_param, 0, sizeof(pub_param));
    pub_param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub_param.message.topic.topic.utf8 = (uint8_t *)TELEMETRY_TOPIC;
    pub_param.message.topic.topic.size = strlen(TELEMETRY_TOPIC);
    pub_param.message.payload.data = payload;
    pub_param.message.payload.len = strlen(payload);
    pub_param.message_id = sys_rand32_get();
    pub_param.dup_flag = 0;
    pub_param.retain_flag = 0;

    /* Publish message */
    ret = mqtt_publish(&client, &pub_param);
    if (ret != 0) {
        LOG_ERR("Failed to publish telemetry: %d", ret);
        return ret;
    }

    LOG_DBG("Telemetry sent successfully");
    return 0;
}

/* Send attributes */
static int send_attributes(const char *firmware_version, const char *device_model)
{
    char payload[128];
    struct mqtt_publish_param pub_param;
    int ret;

    if (!mqtt_connected) {
        return -ENOTCONN;
    }

    snprintf(payload, sizeof(payload),
             "{\"firmwareVersion\":\"%s\",\"deviceModel\":\"%s\"}",
             firmware_version, device_model);

    LOG_INF("Sending attributes: %s", payload);

    memset(&pub_param, 0, sizeof(pub_param));
    pub_param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub_param.message.topic.topic.utf8 = (uint8_t *)ATTRIBUTES_TOPIC;
    pub_param.message.topic.topic.size = strlen(ATTRIBUTES_TOPIC);
    pub_param.message.payload.data = payload;
    pub_param.message.payload.len = strlen(payload);
    pub_param.message_id = sys_rand32_get();
    pub_param.dup_flag = 0;
    pub_param.retain_flag = 0;

    ret = mqtt_publish(&client, &pub_param);
    if (ret != 0) {
        LOG_ERR("Failed to publish attributes: %d", ret);
        return ret;
    }

    LOG_INF("Attributes sent successfully");
    return 0;
}

/* Maintain MQTT connection */
static void mqtt_maintenance(void)
{
    if (!mqtt_connected) {
        return;
    }

    /* Process incoming MQTT messages */
    mqtt_input(&client);

    /* Keep connection alive */
    mqtt_live(&client);
}

/* Main application */
int main(void)
{
    int ret;
    int temperature = 25;
    int humidity = 60;

    LOG_INF("===================================");
    LOG_INF("   ESP32 ThingsBoard Client");
    LOG_INF("   Zephyr RTOS");
    LOG_INF("===================================");

    /* Allow some time for system initialization */
    k_sleep(K_SECONDS(2));

    /* Connect to WiFi */
    ret = wifi_connect();
    if (ret != 0) {
        LOG_ERR("Failed to connect to WiFi: %d", ret);
        return -1;
    }

    k_sleep(K_SECONDS(1));

    /* Resolve ThingsBoard address */
    ret = broker_init();
    if (ret != 0) {
        LOG_ERR("Failed to resolve ThingsBoard address: %d", ret);
        return -1;
    }

    k_sleep(K_SECONDS(1));

    /* Connect to ThingsBoard */
    ret = thingsboard_connect();
    if (ret != 0) {
        LOG_ERR("Failed to connect to ThingsBoard: %d", ret);
        return -1;
    }

    /* Send initial attributes */
    send_attributes("2.0.0", "ESP32-Zephyr");

    LOG_INF("===================================");
    LOG_INF("   Starting data transmission");
    LOG_INF("===================================");

    /* Main application loop */
    while (1) {
        /* Simulate sensor readings */
        temperature += (sys_rand32_get() % 10 - 5) * 1;
        humidity += (sys_rand32_get() % 10 - 5) * 1;

        /* Keep values in reasonable range */
        temperature = CLAMP(temperature, 20.0f, 35.0f);
        humidity = CLAMP(humidity, 40.0f, 80.0f);

        /* Send telemetry data */
        ret = send_telemetry(temperature, humidity);
        if (ret != 0) {
            LOG_WRN("Telemetry send failed, attempting reconnection...");
            thingsboard_connect();
        }

        /* Perform MQTT maintenance */
        mqtt_maintenance();

        /* Wait before next transmission */
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
