// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Left-open escalation wiring (app): NVS-backed config, watchdog task and
// WS endpoints. Pure transition logic lives in left_open.c (host-tested);
// this file is the IDF side (tasks, NVS, broadcasts, actions).

#pragma once

#include "cJSON.h"

// get_left_open / set_left_open endpoint handlers.
void ws_handle_get_left_open(const cJSON *root, int sockfd);
void ws_handle_set_left_open(const cJSON *root, int sockfd);

// Start the 1 s watchdog task (idempotent). Safe before WiFi: with no
// connection the task still tracks state, events just queue nowhere.
void left_open_start(void);
