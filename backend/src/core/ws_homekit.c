// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit status endpoint (core).

#include "ws_homekit.h"

#include "homekit.h"
#include "websocket.h"

#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "WS_HOMEKIT";

static void format_homekit_config(char *json, size_t len)
{
    const char *enabled = homekit_enabled() ? "true" : "false";
    if (!homekit_started())
    {
        snprintf(json, len,
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"enabled\":%s,\"started\":false,\"paired\":false}",
                 enabled);
    }
    else if (homekit_is_paired())
    {
        snprintf(json, len,
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"enabled\":%s,\"started\":true,\"paired\":true}",
                 enabled);
    }
    else
    {
        const char *uri = homekit_setup_payload();
        if (uri)
            snprintf(json, len,
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"enabled\":%s,\"started\":true,\"paired\":false,"
                     "\"setup_uri\":\"%.127s\"}",
                     enabled, uri);
        else
            snprintf(json, len,
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"enabled\":%s,\"started\":true,\"paired\":false}",
                     enabled);
    }
}

void ws_handle_get_homekit(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_homekit");
    char json[256];
    format_homekit_config(json, sizeof(json));
    send_message_sockfd(json, sockfd);
}

void ws_handle_set_homekit(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "set_homekit");
    cJSON *en = cJSON_GetObjectItem(root, "enabled");
    if (!cJSON_IsBool(en))
    {
        char json[160];
        snprintf(json, sizeof(json), "{\"type\":\"error\",\"message\":\"%s\"}",
                 "set_homekit needs enabled:true/false");
        broadcast_message(json);
        return;
    }
    bool enabled = cJSON_IsTrue(en);
    homekit_set_enabled(enabled);
    char json[128];
    snprintf(json, sizeof(json),
             "{\"type\":\"homekit_saved\",\"success\":true}");
    send_message_sockfd(json, sockfd);
    // Fresh state follows by broadcast (set_enabled) or, when nothing
    // changed, is already on screen.
}

void broadcast_homekit_config(void)
{
    char json[256];
    format_homekit_config(json, sizeof(json));
    broadcast_message(json);
}
