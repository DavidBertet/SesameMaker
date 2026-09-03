#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "esp_err.h"
#include <stdbool.h>

// MQTT publishing mode selected in web settings.
//   MQTT_MODE_OFF  - MQTT disabled.
//   MQTT_MODE_MASTER - plain static topics ({prefix}/state, {prefix}/set with a
//                      JSON envelope), usable by any MQTT client / broker.
//   MQTT_MODE_HA   - Home Assistant: adds MQTT discovery on top of the same
//                    {prefix} topics so entities auto-appear (covers the JSON
//                    envelope that manual YAML can't express).
#define MQTT_MODE_OFF 0
#define MQTT_MODE_MASTER 1
#define MQTT_MODE_HA 2

typedef struct
{
  int mode; // one of MQTT_MODE_*
  char uri[128];
  char username[64];
  char password[64];
  char topic_prefix[64];
} mqtt_config_t;

#ifdef __cplusplus
extern "C"
{
#endif

  esp_err_t mqtt_init(void);
  esp_err_t mqtt_get_config(mqtt_config_t *out);
  esp_err_t mqtt_set_config(const mqtt_config_t *cfg);

  // Publish the current garage door state to {prefix}/state
  void mqtt_publish_garage_state(void);

#ifdef __cplusplus
}
#endif
