// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <stdbool.h>

#include "lwip/err.h"

// NTP server configuration
#define NTP_SERVER "pool.ntp.org"
// Set timezone. Examples:
// "EST5EDT,M3.2.0/2,M11.1.0" - Eastern Time
// "PST8PDT,M3.2.0,M11.1.0" - Pacific Time
// "CET-1CEST,M3.5.0,M10.5.0/3" - Central European Time
// "JST-9" - Japan Standard Time
// "UTC-0" - UTC
#define NTP_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"

// Above are build defaults. The effective values live in NVS ("storage"
// namespace, keys below) and are editable over the common settings endpoint;
// a missing/empty key falls back to the define.
#define TIME_CFG_SERVER_MAX 63
#define TIME_CFG_TZ_MAX 63

void start_ntp_sync(void);

// Effective values (NVS, else build default). Always NUL-terminated.
void time_config_get(char *server, size_t server_len, char *tz, size_t tz_len);

// Persist + apply live (TZ immediately, SNTP server on re-init below).
// False on oversize/empty server.
bool time_config_set(const char *server, const char *tz);

// Utility function to check if time is set
bool is_time_set(void);

// Function to register a callback for when time is synchronized
void register_time_sync_callback(esp_err_t (*callback)(void));
