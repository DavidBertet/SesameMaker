// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <esp_http_server.h>

#include <stddef.h>

void start_web_file(httpd_handle_t server);

// Runtime OTA upload password: NVS "ota_password" when set via USB
// provisioning, else the OTA_PASSWORD build default (empty = open).
void ota_password_get(char *buf, size_t len);
