// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "cJSON.h"

void ws_handle_get_zigbee_config(const cJSON *root, int sockfd);
void ws_handle_set_zigbee_config(const cJSON *root, int sockfd);
void ws_handle_zigbee_pair(const cJSON *root, int sockfd);
void ws_handle_zigbee_leave(const cJSON *root, int sockfd);
void ws_handle_zigbee_reset(const cJSON *root, int sockfd);

// Push the current state to every client. Called after local mutations
// (BOOT button) so the webpage reflects changes it didn't initiate,
// plus once per second while a pairing window counts down.
void broadcast_zigbee_config(void);
