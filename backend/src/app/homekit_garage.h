#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic HomeKit transport (app): HAP
// accessory definition (door + lock + light + motion services), inbound
// write mapping, outbound state reporting, identify routine. Registers
// itself on the core homekit service; holds no HAP lifecycle state.

#ifdef __cplusplus
extern "C"
{
#endif

  // Register the garage accessory. Call once before homekit_start().
  void homekit_garage_register(void);

  // Push the current garage state into HAP characteristics (notifies
  // subscribed controllers). Call on every state broadcast; unchanged
  // values are skipped locally.
  void homekit_report_state(void);

#ifdef __cplusplus
}
#endif
