// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Zigbee bridge service (core, 802.15.4 targets only): config state
// machine (enabled flag, scan channel, pairing window, NVS), network
// event latching, BOOT-button pairing/reset, and transport binding.
// Knows nothing about the product: endpoint table, command mapping,
// state reporting and device identity come from a zb_device_t
// registrant (e.g. zigbee_garage). A second product reuses this file
// untouched and only provides its own registrant.

#pragma once

#include "esp_err.h"
#include "zb_transport.h"
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
  // when lqi_valid (cleared on leave/reset, polled on demand while joined).
  uint8_t lqi;
  bool lqi_valid;
  // Parent we are joined through: short address + tree depth (0 = the
  // coordinator itself). Valid together with lqi_valid.
  uint16_t parent_addr;
  uint8_t parent_depth;
} zigbee_state_t;

// Product-specific Zigbee content. Mirrors the transport contract but at
// device level: the service fills the transport table/identity from here
// and routes commands/reports through here.
typedef struct
{
  // Endpoint table builder (see zb_app_t.build_table).
  size_t (*build_table)(zb_endpoint_desc_t *out, size_t max);
  // Length-prefixed ZCL strings ("\\x0b" "Manufacturer").
  const char *manufacturer_name;
  const char *model_identifier;
  // Inbound attribute write for one of our endpoints. value points at
  // zb_attr_type_size(type) little-endian bytes (ZCL wire order).
  void (*on_attr_write)(uint8_t ep, uint16_t cluster_id, uint16_t attr_id,
                        zb_attr_type_t type, const void *value);
  // Push the current device state into zb_transport_report_onoff calls.
  // Invoked by zigbee_report_state(); transport dedupes, so repeats are free.
  void (*report_state)(void);
} zb_device_t;

#ifdef __cplusplus
extern "C"
{
#endif

  esp_err_t zigbee_init(void);

  // Product content wiring. Call once before zigbee_init(); pointers must
  // stay valid (app-static).
  void zigbee_register_device(const zb_device_t *dev);

  // Snapshot for WS responses. On non-Zigbee builds reports enabled=false,
  // joined=false so the UI hides the section.
  esp_err_t zigbee_get_state(zigbee_state_t *out);

  esp_err_t zigbee_set_enabled(bool enabled);
  // Pin the Zigbee scan channel (0 = auto/all, 11..26 = single channel).
  // Persisted; applied on next stack start and, if the stack is already
  // running, immediately via the rejoin/steering path.
  esp_err_t zigbee_set_channel(uint8_t channel);
  // Combined setter for set_zigbee_config: validates everything first,
  // then applies with a single NVS persist + single broadcast.
  esp_err_t zigbee_apply_config(bool has_enabled, bool enabled,
                                bool has_channel, uint8_t channel);
  // Open the joining window for up to duration_s (clamped internally).
  esp_err_t zigbee_start_pairing(uint32_t duration_s);
  // Leave the network but keep the enabled flag/config.
  esp_err_t zigbee_leave(void);
  // BDB factory reset + NVS wipe + rejoin-ready state.
  esp_err_t zigbee_factory_reset(void);

  // Report the latest device state to bound Zigbee clusters (delegates to
  // the registered device). No-op when disabled or unregistered.
  void zigbee_report_state(void);

  // BOOT-button monitor (BOOT strapping pin, runtime sampling only).
  // No-op on targets without a button (ZIGBEE_BOOT_GPIO < 0).
  void zigbee_button_init(void);

#ifdef __cplusplus
}
#endif
