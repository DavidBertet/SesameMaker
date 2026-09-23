// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_zigbee.h"

#include "websocket.h"
#include "zigbee.h"
#include "zb_transport.h"
#include "channel_config.h"

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
             "\"enabled\":%s,\"joined\":%s,\"commissioned\":%s,"
             "\"channel\":%u,\"channel_cfg\":%u,\"pan_id\":%u,\"short_addr\":%u,\"pairing_remaining_s\":%lu,"
             "\"lqi\":%u,\"lqi_valid\":%s,"
             "\"parent_addr\":%u,\"parent_depth\":%u}",
             supported, st.enabled ? "true" : "false",
             st.joined ? "true" : "false", st.commissioned ? "true" : "false",
             st.channel, st.channel_cfg, st.pan_id, st.short_addr,
             (unsigned long)st.pairing_remaining_s,
             st.lqi, st.lqi_valid ? "true" : "false",
             st.parent_addr, st.parent_depth);
    return true;
}

static void send_state(int sockfd)
{
    // Kick a fresh parent-link reading on every read: the reply carries
    // last-known values immediately, the poll result follows by broadcast.
    zb_transport_poll_lqi();
    char json[352];
    if (!format_state(json, sizeof(json)))
    {
        send_error("Failed to read Zigbee state");
        return;
    }
    send_message_sockfd(json, sockfd);
}

void broadcast_zigbee_config(void)
{
    char json[352];
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
    bool has_enabled = cJSON_IsBool(en);
    bool enabled = cJSON_IsTrue(en);
    cJSON *ch = cJSON_GetObjectItem(root, "channel_cfg");
    bool has_channel = false;
    int channel_int = 0;
    if (ch && cJSON_IsNumber(ch))
    {
        // Strict int: reject 11.9-style truncation and out-of-range doubles
        // before the uint8_t cast (300 would otherwise wrap to 44).
        if (ch->valuedouble != (double)ch->valueint)
        {
            send_error("Zigbee scan channel must be 0 (auto) or 11-26");
            return;
        }
        has_channel = true;
        channel_int = ch->valueint;
        if (!zigbee_channel_cfg_valid(channel_int))
        {
            send_error("Zigbee scan channel must be 0 (auto) or 11-26");
            return;
        }
    }
    esp_err_t ret = zigbee_apply_config(has_enabled, enabled,
                                        has_channel, (uint8_t)channel_int);
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        send_error("Zigbee not supported on this build");
        return;
    }
    if (ret != ESP_OK)
    {
        send_error("Zigbee scan channel must be 0 (auto) or 11-26");
        return;
    }
    send_ok("saved", true, sockfd);
    // No unicast echo: apply already broadcast on change (reaches the
    // requester too), and no-change means the requester holds current
    // values. Keep the LQI refresh for the open card.
    zb_transport_poll_lqi();
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
