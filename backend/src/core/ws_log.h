// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "cJSON.h"

void ws_handle_log_start(const cJSON *root, int sockfd);
void ws_handle_log_stop(const cJSON *root, int sockfd);

// Drop a log subscriber whose websocket just disconnected. Without this the
// bridge stays engaged (and keeps forwarding/looping device logs) after the
// debug console or its page closes without a log_stop.
void ws_log_client_disconnected(int sockfd);
void ws_log_init(void);
void ws_log_deinit(void);
