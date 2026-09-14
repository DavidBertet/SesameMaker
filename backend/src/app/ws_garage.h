// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "websocket.h"

void ws_handle_get_garage_status(const cJSON *root, int sockfd);
void ws_handle_garage_door_command(const cJSON *root, int sockfd);
void ws_handle_garage_light_command(const cJSON *root, int sockfd);
void ws_handle_garage_lock_command(const cJSON *root, int sockfd);
void ws_handle_get_garage_raw(const cJSON *root, int sockfd);
void ws_handle_garage_sync(const cJSON *root, int sockfd);
