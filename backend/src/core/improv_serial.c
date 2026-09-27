// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "improv_serial.h"

#include "improv.h"
#include "storage.h"
#include "webfile.h"
#include "wifi.h"

#include "sdkconfig.h"

#include "driver/usb_serial_jtag.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "IMPROV";

#define IMPROV_TASK_STACK 4096
#define IMPROV_TASK_PRIO 5
#define IMPROV_READ_TIMEOUT_MS 20
#define IMPROV_IDLE_RESET_MS 500
#define IMPROV_CONNECT_TIMEOUT_MS 30000
#define IMPROV_CONNECT_POLL_MS 250

#ifndef FW_VERSION
#define FW_VERSION ""
#endif

static TaskHandle_t s_improv_task;
static uint8_t s_state = IMPROV_STATE_AUTHORIZED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Console transport: USB-Serial-JTAG is the primary console (see
// sdkconfig.defaults), but the console writes straight to the FIFO without
// installing the driver — so reads would hard-fault on the NULL driver
// object. Install it ourselves with RX+TX buffers; console output keeps
// working alongside (a log line landing mid-frame just fails the host
// checksum and it retries).
#define IMPROV_JTAG_RX_BUF 512
#define IMPROV_JTAG_TX_BUF 512

static void improv_tx(uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t out[IMPROV_FRAME_MAX];
    size_t n = improv_frame(type, data, len, out, sizeof(out));
    if (n == 0)
    {
        return;
    }
    usb_serial_jtag_write_bytes(out, n, pdMS_TO_TICKS(100));
}

static void send_state(uint8_t state)
{
    s_state = state;
    improv_tx(IMPROV_TYPE_STATE, &state, 1);
}

static void send_error(uint8_t error)
{
    improv_tx(IMPROV_TYPE_ERROR, &error, 1);
}

static void send_result(uint8_t cmd, const char *const *strs, size_t n)
{
    uint8_t payload[IMPROV_FRAME_MAX];
    size_t plen = improv_result_payload(cmd, strs, n, payload, sizeof(payload));
    if (plen == 0 && n > 0)
    {
        send_error(IMPROV_ERROR_UNKNOWN);
        return;
    }
    improv_tx(IMPROV_TYPE_RESULT, payload, plen);
}

// Reachable web UI URL for the provisioning response, or NULL when we have
// no STA address yet (the response then carries an empty string list).
static const char *device_url(void)
{
    static char url[32];
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta)
    {
        return NULL;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(sta, &info) != ESP_OK || info.ip.addr == 0)
    {
        return NULL;
    }
    snprintf(url, sizeof(url), "http://" IPSTR "/", IP2STR(&info.ip));
    return url;
}

static void send_settings_response(uint8_t cmd)
{
    const char *url = device_url();
    if (url)
    {
        send_result(cmd, &url, 1);
    }
    else
    {
        send_result(cmd, NULL, 0);
    }
}

static void send_device_info(void)
{
    const char *version = FW_VERSION[0] ? FW_VERSION : "dev";
    const char *info[] = {"SesameMaker", version, CONFIG_IDF_TARGET, "SesameMaker"};
    send_result(IMPROV_RPC_GET_INFO, info, 4);
}

// Stored credentials imply a previous provisioning (mirrors ESPHome, which
// reports PROVISIONED on has_sta rather than on link).
static uint8_t stored_state(void)
{
    return is_wifi_setup() ? IMPROV_STATE_PROVISIONED : IMPROV_STATE_AUTHORIZED;
}

static void handle_wifi_settings(const uint8_t *payload, size_t plen)
{
    char ssid[IMPROV_SSID_MAX + 1];
    char pass[IMPROV_PASS_MAX + 1];
    if (!improv_wifi_settings_parse(payload, plen, ssid, IMPROV_SSID_MAX, pass, IMPROV_PASS_MAX))
    {
        send_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    // SSID only in logs: it beacons in the clear anyway, the password never.
    ESP_LOGI(TAG, "Provisioning SSID \"%s\" over USB", ssid);
    send_state(IMPROV_STATE_PROVISIONING);
    // Drop any live connection first: otherwise an already-online device
    // reports instant success without ever trying the new credentials.
    if (is_wifi_connected())
    {
        ESP_LOGI(TAG, "Dropping current STA connection before provisioning");
        esp_wifi_disconnect();
        uint32_t gone = now_ms();
        while (is_wifi_connected() && (now_ms() - gone) < 3000)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    esp_err_t err = wifi_start_sta_connection(ssid, pass);
    memset(pass, 0, sizeof(pass)); // creds are copied out synchronously above
    if (err != ESP_OK)
    {
        send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
        send_state(stored_state());
        return;
    }
    uint32_t start = now_ms();
    while (!is_wifi_connected() && (now_ms() - start) < IMPROV_CONNECT_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(IMPROV_CONNECT_POLL_MS));
    }
    // STA keeps retrying in the background either way; report where we stand.
    // Unlike ESPHome we keep stored credentials on timeout (wiping a network
    // over one slow join would strand the device pointlessly).
    if (is_wifi_connected())
    {
        send_state(IMPROV_STATE_PROVISIONED);
        send_settings_response(IMPROV_RPC_WIFI_SETTINGS);
    }
    else
    {
        send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
        send_state(stored_state());
    }
}

static void handle_get_networks(void)
{
    // Blocking scan, same call the portal's wifi_scan uses. Each network
    // goes out as its own [ssid, rssi, auth] response; the empty trailer
    // ends the list (ESP Web Tools renders its picker from this).
    if (esp_wifi_scan_start(NULL, true) != ESP_OK)
    {
        send_error(IMPROV_ERROR_UNKNOWN);
        send_result(IMPROV_RPC_GET_NETWORKS, NULL, 0);
        return;
    }
    uint16_t ap_num = 10;
    wifi_ap_record_t ap_records[10];
    if (esp_wifi_scan_get_ap_records(&ap_num, ap_records) != ESP_OK)
    {
        send_error(IMPROV_ERROR_UNKNOWN);
        send_result(IMPROV_RPC_GET_NETWORKS, NULL, 0);
        return;
    }
    ESP_LOGI(TAG, "Scan found %u networks", (unsigned)ap_num);
    for (uint16_t i = 0; i < ap_num; i++)
    {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", ap_records[i].rssi);
        const char *entry[] = {
            (const char *)ap_records[i].ssid,
            rssi,
            ap_records[i].authmode == WIFI_AUTH_OPEN ? "NO" : "YES",
        };
        send_result(IMPROV_RPC_GET_NETWORKS, entry, 3);
    }
    send_result(IMPROV_RPC_GET_NETWORKS, NULL, 0);
}

static void handle_set_ota_password(const uint8_t *payload, size_t plen)
{
    char pass[IMPROV_OTA_PASS_MAX + 1];
    if (!improv_ota_password_parse(payload, plen, pass, IMPROV_OTA_PASS_MAX))
    {
        send_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    if (pass[0])
    {
        write_str("ota_password", pass);
    }
    else
    {
        // Write empty, don't delete: a missing key regenerates a random
        // password at next boot, an empty one stays open on purpose.
        write_str("ota_password", "");
    }
    memset(pass, 0, sizeof(pass));
    ESP_LOGI(TAG, "OTA password updated over USB");
    send_result(IMPROV_RPC_SET_OTA_PASSWORD, NULL, 0);
}

static void handle_rpc(const uint8_t *data, size_t len)
{
    uint8_t cmd;
    const uint8_t *payload;
    size_t plen;
    if (!improv_rpc_parse(data, len, &cmd, &payload, &plen))
    {
        send_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    switch (cmd)
    {
    case IMPROV_RPC_WIFI_SETTINGS:
        handle_wifi_settings(payload, plen);
        break;
    case IMPROV_RPC_GET_STATE:
        send_state(s_state);
        if (s_state == IMPROV_STATE_PROVISIONED)
        {
            send_settings_response(IMPROV_RPC_GET_STATE);
        }
        break;
    case IMPROV_RPC_GET_INFO:
        send_device_info();
        break;
    case IMPROV_RPC_GET_NETWORKS:
        handle_get_networks();
        break;
    case IMPROV_RPC_SET_OTA_PASSWORD:
        handle_set_ota_password(payload, plen);
        break;
    case IMPROV_RPC_GET_OTA_PASSWORD:
    {
        char current[65];
        ota_password_get(current, sizeof(current));
        send_result(IMPROV_RPC_GET_OTA_PASSWORD, (const char *[]){current}, current[0] ? 1 : 0);
        memset(current, 0, sizeof(current));
        break;
    }
    default:
        // Unimplemented standard commands (hostname, ...) and unknown ones:
        // stock clients fall back gracefully.
        send_error(IMPROV_ERROR_UNKNOWN_RPC);
        break;
    }
}

static void improv_serial_task(void *arg)
{
    (void)arg;
    improv_parser_t parser;
    improv_parser_init(&parser);
    uint32_t last_byte_ms = 0;
    s_state = stored_state();

    uint8_t chunk[64];
    while (1)
    {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(IMPROV_READ_TIMEOUT_MS));
        if (n <= 0)
        {
            continue;
        }
        uint32_t now = now_ms();
        if (parser.pos > 0 && (now - last_byte_ms) > IMPROV_IDLE_RESET_MS)
        {
            improv_parser_init(&parser);
        }
        last_byte_ms = now;
        for (int i = 0; i < n; i++)
        {
            if (improv_parser_feed(&parser, chunk[i]) && parser.ready)
            {
                parser.ready = false;
                if (improv_frame_type(&parser) == IMPROV_TYPE_RPC)
                {
                    handle_rpc(improv_frame_data(&parser), improv_frame_len(&parser));
                }
                // Anything else inbound (stray state/error echoes) is noise.
            }
        }
    }
}

void improv_serial_start(void)
{
    if (!s_improv_task)
    {
        const usb_serial_jtag_driver_config_t cfg = {
            .tx_buffer_size = IMPROV_JTAG_TX_BUF,
            .rx_buffer_size = IMPROV_JTAG_RX_BUF,
        };
        esp_err_t err = usb_serial_jtag_driver_install((usb_serial_jtag_driver_config_t *)&cfg);
        ESP_LOGI(TAG, "JTAG driver install: %s", esp_err_to_name(err));
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "JTAG driver install failed, no USB provisioning");
            return;
        }
        xTaskCreate(improv_serial_task, "improv_serial", IMPROV_TASK_STACK,
                    NULL, IMPROV_TASK_PRIO, &s_improv_task);
        ESP_LOGI(TAG, "Improv task started");
    }
}
