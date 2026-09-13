// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Real 802.15.4 glue (esp-zigbee-lib). Owned by zigbee.c, which keeps the
// user-facing state machine (enabled/pairing window/NVS) and calls in here.
// All stack calls that need ZB-task context go through deferred timers,
// so zigbee.c / WS handlers / button task can call this from any task.

#pragma once

#include "esp_err.h"
#include "garage_controller.h"

#ifdef __cplusplus
extern "C"
{
#endif

  // Start the ZB task (idempotent). No-op until zigbee is enabled — the
  // task is cheap but the radio stays off until commissioning runs.
  void zigbee_stack_start(void);

  // Pin Zigbee scanning to a single channel: 0 = auto (scan all 16,
  // 11..26), or 11..26 to scan only that channel (longer dwell per
  // channel). Invalid values are ignored. Safe from any task; applied in
  // ZB context when the stack is already running, else on next start.
  void zigbee_stack_set_channel(uint8_t channel);

  // Open steering (join/permit-join) via alarm. No-op when the stack is not
  // up yet; the pairing-window countdown in zigbee.c still applies.
  void zigbee_stack_pair(void);

  // Leave the current network (local action). Keeps the enabled flag.
  void zigbee_stack_leave(void);

  // Erase ZB persistence (network state, bindings). Our own NVS blob is
  // wiped by zigbee.c separately.
  void zigbee_stack_factory_reset(void);

  // Push a garage snapshot into ZCL attributes (dedupe inside). Safe from
  // any task: values are latched and applied in ZB context via alarm.
  void zigbee_stack_report(const garage_state_t *st);

  // Request a fresh parent-link LQI reading. Async: the result arrives
  // via broadcast_zigbee_config. Safe from any task, no-op while
  // unjoined or when one is already in flight.
  void zigbee_stack_poll_lqi(void);

#ifdef __cplusplus
}
#endif
