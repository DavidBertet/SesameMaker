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
    if (!homekit_started())
    {
        snprintf(json, len,
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"started\":false,\"paired\":false}");
    }
    else if (homekit_is_paired())
    {
        snprintf(json, len,
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"started\":true,\"paired\":true}");
    }
    else
    {
        const char *uri = homekit_setup_payload();
        if (uri)
            snprintf(json, len,
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"started\":true,\"paired\":false,\"setup_uri\":\"%.127s\"}",
                     uri);
        else
            snprintf(json, len,
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"started\":true,\"paired\":false}");
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

void broadcast_homekit_config(void)
{
    char json[256];
    format_homekit_config(json, sizeof(json));
    broadcast_message(json);
}
