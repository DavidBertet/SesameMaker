#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// MQTT bridge settings endpoints (core): broker config get/set over WS.
// Pure core mqtt API marshaling; no device knowledge.

#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

  void ws_handle_get_mqtt_config(const cJSON *root, int sockfd);
  void ws_handle_set_mqtt_config(const cJSON *root, int sockfd);

#ifdef __cplusplus
}
#endif
