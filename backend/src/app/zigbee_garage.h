#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic Zigbee service (app): endpoint
// table, command mapping, state reporting, device identity. Registers
// itself on the core zigbee service; holds no stack state.

#ifdef __cplusplus
extern "C"
{
#endif

  // Register garage endpoints + mapping. Call once before zigbee_init()
  // so stack startup picks them up.
  void zigbee_garage_register(void);

#ifdef __cplusplus
}
#endif
