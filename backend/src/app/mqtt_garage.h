#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic MQTT bridge (app): command topics,
// state JSON, and Home Assistant discovery. Registers itself on the core
// mqtt.* transport; holds no client state.

#ifdef __cplusplus
extern "C"
{
#endif

  // Register garage topic handlers + state/discovery providers. Call once
  // before mqtt_init() so (re)connects pick them up.
  void mqtt_garage_register(void);

#ifdef __cplusplus
}
#endif
