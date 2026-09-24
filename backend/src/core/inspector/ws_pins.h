// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "websocket.h"

void ws_handle_get_gpio_state(const cJSON *root, int sockfd);
// On-demand bus capture session (edge timestamps + framing analysis).
// Start refreshes a 60 s deadline (heartbeat); stop ends it immediately.
void ws_handle_bus_capture_start(const cJSON *root, int sockfd);
void ws_handle_bus_capture_stop(const cJSON *root, int sockfd);
