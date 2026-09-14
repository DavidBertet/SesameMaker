#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Generic MQTT bridge transport (core). Owns broker config + persistence,
// client lifecycle, topic-prefix plumbing, and a subscription/state/
// discovery registry. Knows nothing about the device: the app registers
// topic handlers, a state provider, and a discovery provider.

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

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

// Inbound topic handler. suffix is the part after "{prefix}/" matched
// exactly (e.g. "door/set"); data is NOT nul-terminated, use data_len.
typedef void (*mqtt_topic_cb_t)(const char *suffix, const char *data,
                                int data_len);
// State provider. Fills payload (nul-terminated, fits in len) and returns
// true, or false when there is nothing to publish.
typedef bool (*mqtt_state_provider_t)(char *payload, size_t len);
// Discovery provider. Publishes entity configs via mqtt_publish_discovery.
typedef void (*mqtt_discovery_provider_t)(void);

#ifdef __cplusplus
extern "C"
{
#endif

  esp_err_t mqtt_init(void);
  esp_err_t mqtt_get_config(mqtt_config_t *out);
  esp_err_t mqtt_set_config(const mqtt_config_t *cfg);

  // Publish to "{prefix}/{suffix}". No-op error when disabled/disconnected.
  esp_err_t mqtt_publish(const char *suffix, const char *payload, int qos,
                         bool retain);
  // Publish an HA discovery config for one entity.
  void mqtt_publish_discovery(const char *component, const char *object_id,
                              const char *json_config);

  // App wiring (call before/after mqtt_init; table survives reconnects).
  // False when the table is full / suffix already registered.
  bool mqtt_register_topic_handler(const char *suffix, mqtt_topic_cb_t cb);
  void mqtt_register_state_provider(mqtt_state_provider_t fn);
  void mqtt_register_discovery_provider(mqtt_discovery_provider_t fn);

  // Ask for a state publish (dedupe inside: identical payloads are
  // skipped). The app calls this whenever its state changes; core also
  // publishes on (re)connect automatically.
  void mqtt_notify_changed(void);

#ifdef __cplusplus
}
#endif
