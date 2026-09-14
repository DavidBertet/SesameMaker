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

#ifdef __cplusplus
}
#endif
