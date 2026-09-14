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
  // Network credentials stored (paired at some point), even if currently
  // disconnected — e.g. reboot while the coordinator is down. Lets the UI
  // show "Reconnecting…" instead of the misleading "Not joined".
  bool commissioned;
  uint16_t channel;
  uint16_t pan_id;
  // Configured scan channel: 0 = auto (all 16), 11..26 = pinned. read-only
  // for the UI, settable via set_zigbee_config.
  uint8_t channel_cfg;
  // Seconds remaining in the pairing window, 0 = closed.
  uint32_t pairing_remaining_s;
  // Parent-link LQI (0-255) from the last neighbor-table poll; valid only
  // when lqi_valid (cleared on leave/reset, polled ~1/min while joined).
  uint8_t lqi;
  bool lqi_valid;
  // Parent we are joined through: short address + tree depth (0 = the
  // coordinator itself). Valid together with lqi_valid.
  uint16_t parent_addr;
  uint8_t parent_depth;
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
  // Pin the Zigbee scan channel (0 = auto/all, 11..26 = single channel).
  // Persisted; applied on next stack start and, if the stack is already
  // running, immediately via the rejoin/steering path.
  esp_err_t zigbee_set_channel(uint8_t channel);
  // Combined setter for set_zigbee_config: validates everything first,
  // then applies with a single NVS persist + single broadcast (only when
  // something actually changed). Fields without has_ set are untouched.
  esp_err_t zigbee_apply_config(bool has_enabled, bool enabled,
                                bool has_channel, uint8_t channel);
  // Open the joining window for up to duration_s (clamped internally).
  esp_err_t zigbee_start_pairing(uint32_t duration_s);
  // Leave the network but keep the enabled flag/config.
  esp_err_t zigbee_leave(void);
  // BDB factory reset + NVS wipe + rejoin-ready state.
  esp_err_t zigbee_factory_reset(void);

  // Report the latest garage state to bound Zigbee clusters (dedupe inside).
  // Called next to mqtt_notify_changed(); no-op when disabled.
  void zigbee_report_state(void);

  // BOOT-button monitor (BOOT strapping pin, runtime sampling only).
  // No-op on targets without a button (ZIGBEE_BOOT_GPIO < 0).
  void zigbee_button_init(void);

  // Internal stack upcalls / queries (used by zigbee_stack.c).
  bool zigbee_pairing_open(void);
  void zigbee_on_joined(uint16_t channel, uint16_t pan_id);
  void zigbee_on_left(void);
  // Stack upcall: network credentials present (production config restored,
  // rejoin in progress) or absent (factory-new / wiped). Cleared by
  // zigbee_on_left; set alongside zigbee_on_joined.
  void zigbee_on_commissioned(bool commissioned);
  void zigbee_on_parent(uint16_t addr, uint8_t depth, uint8_t lqi);

#ifdef __cplusplus
}
#endif
