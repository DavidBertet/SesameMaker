// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "cJSON.h"

void ws_handle_get_settings(const cJSON *root, int sockfd);
void broadcast_get_settings(void);
void ws_handle_time_update(const cJSON *root, int sockfd);
void ws_handle_system_info(const cJSON *root, int sockfd);

// Product feature flags for the settings JSON, as inner object pairs
// (e.g. "\"zigbee\":true"). Registered once by the app; core emits
// them under "features". Pointer must stay valid (app-static literal).
void ws_settings_register_features(const char *json_pairs);