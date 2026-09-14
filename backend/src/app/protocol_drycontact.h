// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "drycontact.h"
#include "esp_err.h"
#include <stdint.h>

// Persistent dry-contact config (NVS blob "dry_cfg"). GPIOs are runtime
// values; the constants.h DRY_* defaults apply on first boot or after a
// size-mismatched (migrated) blob.
typedef struct
{
    int8_t relay_gpio;
    int8_t open_gpio;
    int8_t close_gpio;
    uint8_t sensor_mode; // dry_sensor_mode_t
    uint8_t active_low;  // 1 = reed pulls low when hit (typical w/ pullup)
    uint16_t pulse_ms;   // relay hold time per press
    uint16_t debounce_ms;
    uint16_t travel_s; // door travel estimate for motion inference
} dry_cfg_t;

void dry_cfg_defaults(dry_cfg_t *out);

esp_err_t protocol_drycontact_init(void);
esp_err_t protocol_drycontact_start(void);

// Fire a generic door target (DRY_DOOR_* or DRY_TARGET_TOGGLE). Pulses only
// when drycontact_pulse_needed says so for the given current state.
esp_err_t protocol_drycontact_command(int target, int current);

// Re-sample the reeds immediately (used by garage_sync).
void protocol_drycontact_resync(void);

esp_err_t protocol_drycontact_get_cfg(dry_cfg_t *out);
esp_err_t protocol_drycontact_set_cfg(const dry_cfg_t *cfg);
