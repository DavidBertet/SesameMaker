// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Manual protocol selection (no auto-detect). The UI lists every known id,
// marks unsupported ones via "supported", and adapts itself from the caps
// object.

#include "ws_protocol.h"

#include "garage_controller.h"
#include "homekit.h"
#include "protocol_drycontact.h"
#include "protocol_registry.h"
#include "websocket.h"
#include "zigbee.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "WS_PROTOCOL";

static void reboot_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

// Reboot shortly after the responses flush (WS send is queued).
static void schedule_reboot(uint32_t delay_ms)
{
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = &reboot_timer_cb,
        .name = "proto_reboot",
    };
    if (esp_timer_create(&args, &timer) != ESP_OK)
    {
        esp_restart();
        return;
    }
    if (esp_timer_start_once(timer, (uint64_t)delay_ms * 1000) != ESP_OK)
    {
        esp_restart();
    }
}

static void send_error(const char *message)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"type\":\"error\",\"message\":\"%s\"}", message);
    broadcast_message(json);
}

static void send_dry_json(char *buf, size_t len)
{
    dry_cfg_t cfg;
    if (protocol_drycontact_get_cfg(&cfg) != ESP_OK)
    {
        snprintf(buf, len, "\"dry\":null");
        return;
    }
    snprintf(buf, len,
             "\"dry\":{"
             "\"relay_gpio\":%d,\"open_gpio\":%d,\"close_gpio\":%d,"
             "\"sensor_mode\":%d,\"active_low\":%s,"
             "\"pulse_ms\":%d,\"debounce_ms\":%d,\"travel_s\":%d}",
             cfg.relay_gpio, cfg.open_gpio, cfg.close_gpio, cfg.sensor_mode,
             cfg.active_low ? "true" : "false", cfg.pulse_ms, cfg.debounce_ms,
             cfg.travel_s);
}

static void send_protocol(int sockfd)
{
    protocol_id_t id = protocol_registry_get();
    protocol_caps_t caps = protocol_registry_caps();
    char caps_json[160];
    protocol_caps_json(&caps, caps_json, sizeof(caps_json));
    char dry_json[192];
    send_dry_json(dry_json, sizeof(dry_json));
    char json[448];
    snprintf(json, sizeof(json),
             "{\"type\":\"protocol\",\"id\":\"%s\",\"supported\":%s,\"caps\":{%s},%s}",
             protocol_id_str(id), protocol_id_supported(id) ? "true" : "false",
             caps_json, dry_json);
    send_message_sockfd(json, sockfd);
}

void ws_handle_get_protocol(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_protocol");
    send_protocol(sockfd);
}

static int cfg_int(const cJSON *root, const char *key, int fallback)
{
    cJSON *node = cJSON_GetObjectItem(root, key);
    return cJSON_IsNumber(node) ? node->valueint : fallback;
}

void ws_handle_set_protocol(const cJSON *root, int sockfd)
{
    cJSON *node = cJSON_GetObjectItem(root, "id");
    const char *id_str = cJSON_IsString(node) ? node->valuestring : NULL;
    ESP_LOGI(TAG, "set_protocol: %s", id_str ? id_str : "(null)");
    protocol_id_t id;
    if (!protocol_id_from_str(id_str, &id))
    {
        send_error("Unknown protocol id (secplus1|drycontact|secplus2)");
        return;
    }
    if (!protocol_id_supported(id))
    {
        send_error("Protocol not supported yet");
        return;
    }
    protocol_id_t prev_id = protocol_registry_get();
    // Optional dry-contact settings ride along in the same call so the UI
    // needs a single save path regardless of protocol.
    cJSON *dry = cJSON_GetObjectItem(root, "dry");
    if (cJSON_IsObject(dry))
    {
        dry_cfg_t cfg;
        if (protocol_drycontact_get_cfg(&cfg) != ESP_OK)
        {
            send_error("Failed to read dry-contact config");
            return;
        }
        cfg.relay_gpio = (int8_t)cfg_int(dry, "relay_gpio", cfg.relay_gpio);
        cfg.open_gpio = (int8_t)cfg_int(dry, "open_gpio", cfg.open_gpio);
        cfg.close_gpio = (int8_t)cfg_int(dry, "close_gpio", cfg.close_gpio);
        cfg.sensor_mode = (uint8_t)cfg_int(dry, "sensor_mode", cfg.sensor_mode);
        cJSON *al = cJSON_GetObjectItem(dry, "active_low");
        if (cJSON_IsBool(al))
        {
            cfg.active_low = cJSON_IsTrue(al) ? 1 : 0;
        }
        cfg.pulse_ms = (uint16_t)cfg_int(dry, "pulse_ms", cfg.pulse_ms);
        cfg.debounce_ms = (uint16_t)cfg_int(dry, "debounce_ms", cfg.debounce_ms);
        cfg.travel_s = (uint16_t)cfg_int(dry, "travel_s", cfg.travel_s);
        if (protocol_drycontact_set_cfg(&cfg) != ESP_OK)
        {
            send_error("Invalid dry-contact settings");
            return;
        }
    }
    if (protocol_registry_set(id) != ESP_OK)
    {
        send_error("Failed to persist protocol selection");
        return;
    }
    garage_controller_refresh_protocol();
    send_protocol(sockfd);
    // Push a fresh status frame so every client re-renders from new caps.
    char status[512];
    garage_controller_get_status_json(status, sizeof(status));
    broadcast_message(status);
    if (id != prev_id)
    {
        // Endpoints + accessory follow protocol caps: a configured network
        // or a running accessory no longer matches, so leave and reboot to
        // rebuild from the new caps.
        bool rebuild = false;
        zigbee_state_t zs;
        if (zigbee_get_state(&zs) == ESP_OK && (zs.joined || zs.commissioned))
        {
            ESP_LOGW(TAG, "Protocol switched while configured: leaving Zigbee");
            zigbee_leave();
            rebuild = true;
        }
        if (homekit_enabled())
        {
            ESP_LOGW(TAG, "Protocol switched while HomeKit running: rebooting to rebuild accessory");
            rebuild = true;
        }
        if (rebuild)
            schedule_reboot(800);
    }
}
