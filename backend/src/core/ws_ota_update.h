// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "cJSON.h"

// Self-update from GitHub releases (core): check_update reports the
// verified manifest version vs running; start_update flashes it in the
// background (progress/result as ota_progress broadcasts).
void ws_handle_check_update(const cJSON *root, int sockfd);
void ws_handle_start_update(const cJSON *root, int sockfd);
