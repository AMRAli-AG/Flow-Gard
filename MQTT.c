/**
 * @file main.c
 * @brief ESP32 Ultrasonic Water Meter IoT System (Zephyr RTOS)
 * @version 1.0.0
 * @date 2025-11-14
 * @Autho: AMR ALI
 *
 * @copyright Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 * 
 * @details
 * This firmware implements a smart water meter system using ESP32 and Zephyr RTOS.
 * It simulates ultrasonic water flow measurements and transmits real-time telemetry
 * data to ThingsBoard cloud platform via MQTT protocol.
 * 
 * Key Features:
 * - WiFi connectivity with auto-reconnection
 * - MQTT communication with ThingsBoard
 * - Real-time flow rate monitoring (5-50 L/h)
 * - Total volume tracking (liters)
 * - Leak detection (simulated, 5% probability)
 * - Telemetry transmitted every 5 seconds
 * - Device attributes reporting on startup
 * 
 * Architecture:
 *   ESP32 <--WiFi--> Router <--Internet--> ThingsBoard Cloud
 *   
 * Data Flow:
 *   1. WiFi Connection → 2. IP Address Acquisition → 3. DNS Resolution
 *   4. MQTT Broker Initialization → 5. MQTT Client Connect
 *   6. Send Device Attributes → 7. Periodic Telemetry Transmission
 * 
 * Dependencies:
 * - Zephyr Networking stack (CONFIG_NET_TCP, CONFIG_NET_IPV4, CONFIG_NET_MGMT)
 * - Zephyr WiFi driver (ESP32)
 * - Zephyr MQTT client library
 * - Logging subsystem
 *
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
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(thingsboard, LOG_LEVEL_INF);

#define WIFI_SSID "!!Huawei"
#define WIFI_PSK "karamr195"
#define THINGSBOARD_HOST "thingsboard.cloud"
#define THINGSBOARD_PORT 1883
#define ACCESS_TOKEN "JqkpupDR1nmXD6nbZX2S"
#define TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define ATTRIBUTES_TOPIC "v1/devices/me/attributes"
#define RX_BUFFER_SIZE 1024
#define TX_BUFFER_SIZE 1024

static struct mqtt_client client;
static struct sockaddr_in broker_addr;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_obtained, 0, 1);
static volatile bool mqtt_connected = false;

static int32_t total_volume = 0;
static int32_t flow_rate = 15;

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
        LOG_INF("IPv4 obtained");
        k_sem_give(&ipv4_obtained);
    }
}

static int wifi_connect(void)
{
    struct net_if *iface;
    struct wifi_connect_req_params params;

    LOG_INF("Starting WiFi...");

    iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("No WiFi interface");
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

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (ret) {
        LOG_ERR("WiFi request failed: %d", ret);
        return ret;
    }

    if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
        LOG_ERR("WiFi timeout");
        return -ETIMEDOUT;
    }

    if (k_sem_take(&ipv4_obtained, K_SECONDS(30)) != 0) {
        LOG_ERR("IP timeout");
        return -ETIMEDOUT;
    }

    LOG_INF("WiFi ready");
    return 0;
}

static int broker_init(void)
{
    struct zsock_addrinfo hints;
    struct zsock_addrinfo *result;

    LOG_INF("Resolving %s", THINGSBOARD_HOST);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = zsock_getaddrinfo(THINGSBOARD_HOST, NULL, &hints, &result);
    if (ret != 0 || result == NULL) {
        LOG_ERR("DNS failed");
        return -EINVAL;
    }

    memcpy(&broker_addr, result->ai_addr, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(THINGSBOARD_PORT);

    zsock_freeaddrinfo(result);
    LOG_INF("Broker resolved");
    return 0;
}

static void mqtt_evt_handler(struct mqtt_client *const client,
                            const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT refused: %d", evt->result);
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
    default:
        break;
    }
}

static void prepare_mqtt_client(void)
{
    static char client_id[32];
    static struct mqtt_utf8 mqtt_user_name;

    snprintf(client_id, sizeof(client_id), "esp32_%08x", 
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
    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);
    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);
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
                LOG_INF("ThingsBoard connected");
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
        k_sleep(K_SECONDS(2));
    }

    LOG_ERR("Connection failed");
    return -ECONNREFUSED;
}

static int send_telemetry(void)
{
    char payload[256];
    int32_t leak = 0;

    if (!mqtt_connected) return -ENOTCONN;

    flow_rate += (sys_rand32_get() % 11) - 5;
    if (flow_rate < 5) flow_rate = 5;
    if (flow_rate > 50) flow_rate = 50;

    if ((sys_rand32_get() % 100) < 5) {
        leak = 1;
        flow_rate += 20;
    }

    total_volume += flow_rate / 6;
    if (total_volume > 999999) total_volume = 999999;

    snprintf(payload, sizeof(payload),
             "{\"volume\":%ld,\"flowRate\":%ld,\"leak\":%ld}",
             (long)total_volume, (long)flow_rate, (long)leak);

    LOG_INF("TX: %s", payload);

    struct mqtt_publish_param pub = {0};
    pub.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub.message.topic.topic.utf8 = (uint8_t *)TELEMETRY_TOPIC;
    pub.message.topic.topic.size = strlen(TELEMETRY_TOPIC);
    pub.message.payload.data = (uint8_t *)payload;
    pub.message.payload.len = strlen(payload);
    pub.message_id = sys_rand32_get();

    int rc = mqtt_publish(&client, &pub);
    if (rc) LOG_ERR("publish failed: %d", rc);
    return rc;
}

static int send_attributes(void)
{
    char payload[128];
    struct mqtt_publish_param pub = {0};

    if (!mqtt_connected) return -ENOTCONN;

    snprintf(payload, sizeof(payload),
             "{\"firmwareVersion\":\"1.0.0\",\"deviceModel\":\"Water-Meter\"}");

    pub.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    pub.message.topic.topic.utf8 = (uint8_t *)ATTRIBUTES_TOPIC;
    pub.message.topic.topic.size = strlen(ATTRIBUTES_TOPIC);
    pub.message.payload.data = payload;
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

int main(void)
{
    LOG_INF("Water Meter Starting...");
    k_sleep(K_SECONDS(2));

    if (wifi_connect() != 0) return -1;
    k_sleep(K_SECONDS(1));

    if (broker_init() != 0) return -1;
    k_sleep(K_SECONDS(1));

    if (thingsboard_connect() != 0) return -1;

    send_attributes();
    LOG_INF("Data transmission active");

    while (1) {
        if (send_telemetry() != 0) {
            LOG_WRN("Reconnecting...");
            thingsboard_connect();
        }

        mqtt_maintenance();
        k_sleep(K_SECONDS(5));
    }

    return 0;
}
