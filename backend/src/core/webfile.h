// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <esp_http_server.h>

#include <stddef.h>

void start_web_file(httpd_handle_t server);

// Pre-OTA hook: the app registers a safety stop here (motors off, door
// idle) that runs before any firmware flash begins. Core never knows what
// the app stops — it just calls it. NULL (default) means nothing to stop.
void webfile_register_pre_ota(void (*cb)(void));

// Runtime OTA upload password: NVS "ota_password" when set via USB
// provisioning, else the OTA_PASSWORD build default (empty = open).
void ota_password_get(char *buf, size_t len);

// First boot with no password anywhere: generate a random one into NVS and
// print it to USB serial exactly once. Call once at startup.
void ota_password_ensure_generated(void);
