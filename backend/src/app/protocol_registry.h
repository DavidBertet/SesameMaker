// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "esp_err.h"
#include "protocol.h"

// RAM-active protocol id, persisted to NVS. Manual selection only (no
// auto-detect): the UI sends set_protocol, the id takes effect live for
// status/caps/guards and is reloaded at boot.
esp_err_t protocol_registry_init(void);
protocol_id_t protocol_registry_get(void);
// Persists and activates. Unknown/unsupported ids are rejected.
esp_err_t protocol_registry_set(protocol_id_t id);
protocol_caps_t protocol_registry_caps(void);
