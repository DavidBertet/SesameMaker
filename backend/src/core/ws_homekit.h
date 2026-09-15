#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit status endpoint (core): pairing state + setup payload for the
// QR code. Pure core homekit API marshaling; no device knowledge. The
// setup URI is only served while unpaired.

#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

  void ws_handle_get_homekit(const cJSON *root, int sockfd);
  void ws_handle_set_homekit(const cJSON *root, int sockfd);

  // Push current state to all clients (start, pair, unpair). The card
  // subscribes to homekit_config, so no polling is needed.
  void broadcast_homekit_config(void);

#ifdef __cplusplus
}
#endif
