// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "cJSON.h"

void ws_handle_get_protocol(const cJSON *root, int sockfd);
void ws_handle_set_protocol(const cJSON *root, int sockfd);
