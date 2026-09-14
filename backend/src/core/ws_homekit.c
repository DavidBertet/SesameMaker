// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit status endpoint (core).

#include "ws_homekit.h"

#include "homekit.h"
#include "websocket.h"

#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "WS_HOMEKIT";

void ws_handle_get_homekit(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_homekit");
    char json[256];
    if (!homekit_started())
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"started\":false,\"paired\":false}");
    }
    else if (homekit_is_paired())
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"homekit_config\",\"supported\":true,"
                 "\"started\":true,\"paired\":true}");
    }
    else
    {
        const char *uri = homekit_setup_payload();
        if (uri)
            snprintf(json, sizeof(json),
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"started\":true,\"paired\":false,\"setup_uri\":\"%.127s\"}",
                     uri);
        else
            snprintf(json, sizeof(json),
                     "{\"type\":\"homekit_config\",\"supported\":true,"
                     "\"started\":true,\"paired\":false}");
    }
    send_message_sockfd(json, sockfd);
}
