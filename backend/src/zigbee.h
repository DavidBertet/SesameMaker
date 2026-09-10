// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zigbee_press.h"

typedef struct
{
  bool enabled;
  bool joined; // observed network state (read-only for the UI)
  uint16_t channel;
  uint16_t pan_id;
  // Seconds remaining in the pairing window, 0 = closed.
  uint32_t pairing_remaining_s;
} zigbee_state_t;

#ifdef __cplusplus
extern "C"
{
#endif

  esp_err_t zigbee_init(void);

  // Snapshot for WS responses. On non-Zigbee builds reports enabled=false,
  // joined=false so the UI hides the section.
  esp_err_t zigbee_get_state(zigbee_state_t *out);

  esp_err_t zigbee_set_enabled(bool enabled);
  // Open the joining window for up to duration_s (clamped internally).
  esp_err_t zigbee_start_pairing(uint32_t duration_s);
  // Leave the network but keep the enabled flag/config.
  esp_err_t zigbee_leave(void);
  // BDB factory reset + NVS wipe + rejoin-ready state.
  esp_err_t zigbee_factory_reset(void);

  // Report the latest garage state to bound Zigbee clusters (dedupe inside).
  // Called next to mqtt_publish_garage_state(); no-op when disabled.
  void zigbee_report_state(void);

  // BOOT-button monitor (BOOT strapping pin, runtime sampling only).
  // No-op on targets without a button (ZIGBEE_BOOT_GPIO < 0).
  void zigbee_button_init(void);

#ifdef __cplusplus
}
#endif
