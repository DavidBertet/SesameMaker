// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_taskmon.h"

#include "task_monitor.h"

#include "websocket.h"

#include "esp_log.h"

#include <stdlib.h>

static const char *TAG = "WS_TASKMON";

void ws_handle_get_taskmon(const cJSON *root, int sockfd)
{
    (void)root;
    // Size first (json_writer sizing semantics), then fill. ~24 tasks fit
    // comfortably; pathological counts just truncate safely.
    size_t need = task_monitor_json(NULL, 0);
    char *json = malloc(need + 1);
    if (!json)
    {
        send_message_sockfd("{\"type\":\"taskmon\",\"tasks\":[]}", sockfd);
        return;
    }
    task_monitor_json(json, need + 1);
    ESP_LOGI(TAG, "Sent task monitor snapshot (%d bytes)", (int)need);
    send_message_sockfd(json, sockfd);
    free(json);
}
