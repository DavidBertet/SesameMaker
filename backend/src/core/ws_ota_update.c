// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_ota_update.h"

#include "ota_update.h"
#include "websocket.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "WS_OTA_UPDATE";

void ws_handle_check_update(const cJSON *root, int sockfd)
{
    (void)root;
    (void)sockfd;
    ESP_LOGI(TAG, "check_update");

    // Fetch + TLS + verify need far more stack than the httpd task has, so
    // this only spawns the worker — the update_status broadcast follows.
    // No direct reply: the UI already spins until the broadcast lands.
    if (ota_update_in_progress())
    {
        size_t written = 0, total = 0;
        ota_update_progress(&written, &total);
        char json[192];
        snprintf(json, sizeof(json),
                 "{\"type\":\"update_status\",\"available\":true,\"current\":\"%.31s\","
                 "\"progress\":{\"written\":%d,\"total\":%d}}",
                 ota_current_version(), (int)written, (int)total);
        broadcast_message(json);
        return;
    }
    ota_check_task();
}

void ws_handle_start_update(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "start_update");

    char json[128];
    if (ota_update_in_progress())
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"ota_progress\",\"phase\":\"done\",\"success\":false,"
                 "\"message\":\"update already running\"}");
        send_message_sockfd(json, sockfd);
        return;
    }
    snprintf(json, sizeof(json), "{\"type\":\"ota_progress\",\"phase\":\"start\",\"success\":true}");
    send_message_sockfd(json, sockfd);
    ota_start_task();
}
