// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <stdbool.h>
#include <stddef.h>

// Self-update from GitHub releases. Trust chain, fail closed:
// TLS (IDF cert bundle) + ECDSA-P256 manifest signature (offline key, only
// the public half baked here) + chip/semver guards + per-image SHA256.
// Unsigned manifests, wrong chips and downgrades are refused before any
// flash write. Bootloader/partitions are never touched OTA — app slot only.
// The WS layer (ws_ota_update.c) maps these onto endpoints; progress and
// results go out as broadcasts so every client sees them.

// Verified manifest: version/chip/app image only (browser `files` ignored).
typedef struct
{
    char version[32];
    char chip[24];
    char app_name[128];
    char app_sha256[65];
    size_t app_size;
} ota_manifest_t;

// Fetch + signature-verify + parse the release manifest. False with a
// human-readable reason in err (always NUL-terminated).
bool ota_fetch_manifest(ota_manifest_t *out, char *err, size_t errlen);

// True when v is strictly newer than the running FW_VERSION. Non-parseable
// running versions (dev builds) count as older than any release.
bool ota_is_newer(const char *v);

const char *ota_current_version(void);
const char *ota_chip_name(void);

// Re-verifies the manifest, downloads the app image (SHA256-checked) into
// the inactive ota slot, sets boot and reboots. No-op when already running.
void ota_start_task(void);
bool ota_update_in_progress(void);

// Current download progress while running (false when idle). Lets a client
// that (re)connects mid-update pick up the stream via check_update.
bool ota_update_progress(size_t *written, size_t *total);

// Same fetch+verify as above, but in a worker task (TLS needs far more
// stack than the httpd task has) reporting via update_status broadcast.
void ota_check_task(void);
