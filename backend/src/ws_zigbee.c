// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_zigbee.h"

#include "websocket.h"
#include "zigbee.h"

#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "WS_ZIGBEE";

static void send_error(const char *message)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"type\":\"error\",\"message\":\"%s\"}", message);
    broadcast_message(json);
}

static bool format_state(char *json, size_t len)
{
    zigbee_state_t st;
    if (zigbee_get_state(&st) != ESP_OK)
    {
        return false;
    }
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
    const char *supported = "true";
#else
    const char *supported = "false";
#endif
    snprintf(json, len,
             "{\"type\":\"zigbee_config\",\"supported\":%s,"
             "\"enabled\":%s,\"joined\":%s,"
             "\"channel\":%u,\"pan_id\":%u,\"pairing_remaining_s\":%lu}",
             supported, st.enabled ? "true" : "false",
             st.joined ? "true" : "false", st.channel, st.pan_id,
             (unsigned long)st.pairing_remaining_s);
    return true;
}

static void send_state(int sockfd)
{
    char json[256];
    if (!format_state(json, sizeof(json)))
    {
        send_error("Failed to read Zigbee state");
        return;
    }
    send_message_sockfd(json, sockfd);
}

void broadcast_zigbee_config(void)
{
    char json[256];
    if (!format_state(json, sizeof(json)))
    {
        return;
    }
    broadcast_message(json);
}

static void send_ok(const char *op, bool ok, int sockfd)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"type\":\"zigbee_%s\",\"success\":%s}", op, ok ? "true" : "false");
    send_message_sockfd(json, sockfd);
}

void ws_handle_get_zigbee_config(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_zigbee_config");
    send_state(sockfd);
}

void ws_handle_set_zigbee_config(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "set_zigbee_config");
    cJSON *en = cJSON_GetObjectItem(root, "enabled");
    bool enabled = cJSON_IsTrue(en);
    if (zigbee_set_enabled(enabled) != ESP_OK)
    {
        send_error("Zigbee not supported on this build");
        return;
    }
    send_ok("saved", true, sockfd);
    send_state(sockfd);
}

void ws_handle_zigbee_pair(const cJSON *root, int sockfd)
{
    cJSON *dur = cJSON_GetObjectItem(root, "duration_s");
    uint32_t secs = (dur && cJSON_IsNumber(dur)) ? (uint32_t)dur->valueint : 60;
    ESP_LOGI(TAG, "zigbee_pair %lus", (unsigned long)secs);
    send_ok("pair", zigbee_start_pairing(secs) == ESP_OK, sockfd);
    send_state(sockfd);
}

void ws_handle_zigbee_leave(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "zigbee_leave");
    send_ok("leave", zigbee_leave() == ESP_OK, sockfd);
    send_state(sockfd);
}

void ws_handle_zigbee_reset(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "zigbee_reset");
    send_ok("reset", zigbee_factory_reset() == ESP_OK, sockfd);
    send_state(sockfd);
}
