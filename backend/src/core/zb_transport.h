#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Zigbee transport (core): 802.15.4 task lifecycle, BDB commissioning,
// pairing-window-driven steering, parent-link LQI polling, scan-channel
// control, and On/Off attribute reporting. Knows nothing about the device:
// the app describes its endpoints and handles commands/events through
// zb_app_t. A second product (e.g. an air-quality sensor) reuses this
// file untouched and only provides its own table + callbacks.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ZB_TRANSPORT_MAX_ENDPOINTS 8

  // One plain-switch endpoint. Core builds the stock On/Off template
  // (Basic + Identify + Groups + Scenes + On/Off servers, stamped with the
  // app's manufacturer/model) around each entry; device_id overrides the
  // template default (e.g. On/Off Output vs On/Off Light).
  typedef struct
  {
    uint8_t ep;
    uint16_t device_id;
  } zb_endpoint_desc_t;

  typedef struct
  {
    // Endpoint table builder, called once at stack startup (ZB context).
    // Fills up to max entries, returns the count. Read caps/config here
    // (e.g. conditional endpoints per active protocol).
    size_t (*build_table)(zb_endpoint_desc_t *out, size_t max);
    // Length-prefixed ZCL strings ("\\x0b" "Manufacturer"), app identity.
    const char *manufacturer_name;
    const char *model_identifier;
    // Inbound On/Off command for one of our endpoints (on = active).
    void (*on_onoff_cmd)(uint8_t ep, bool on);
    // Network events (ZB context, keep them short: latch + broadcast).
    void (*on_joined)(uint16_t channel, uint16_t pan_id);
    void (*on_left)(void);
    void (*on_commissioned)(bool commissioned);
    void (*on_parent)(uint16_t addr, uint8_t depth, uint8_t lqi);
    // True while the app's pairing window is open (gates steering).
    bool (*pairing_open)(void);
  } zb_app_t;

  // Bind the app contract. Call once before zb_transport_start(); the
  // struct is copied, pointed-to strings/table data must stay valid.
  void zb_transport_register(const zb_app_t *app);

  // Start the ZB task (idempotent). No-op until the app enables its radio.
  void zb_transport_start(void);

  // Pin scanning to a single channel: 0 = auto (all 16, 11..26), or
  // 11..26 for one channel (longer dwell). Invalid values are ignored.
  // Safe from any task; applied in ZB context when already running,
  // else on next start.
  void zb_transport_set_channel(uint8_t channel);

  // Open steering (join/permit-join) via alarm. No-op when the stack is
  // not up yet; the app's pairing-window countdown still applies.
  void zb_transport_pair(void);

  // Leave the current network (local action). Keeps the app's config.
  void zb_transport_leave(void);

  // Erase ZB persistence (network state, bindings).
  void zb_transport_factory_reset(void);

  // Report an On/Off attribute value (dedupe inside; applied in ZB
  // context via alarm). Safe from any task.
  void zb_transport_report_onoff(uint8_t ep, bool on);

  // Request a fresh parent-link LQI reading. Async: the result arrives
  // via the on_parent callback. Safe from any task, no-op while
  // unjoined or when one is already in flight.
  void zb_transport_poll_lqi(void);

#ifdef __cplusplus
}
#endif
