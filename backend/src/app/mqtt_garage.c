// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic MQTT bridge (app).

#include "mqtt_garage.h"

#include "mqtt.h"

#include "garage_controller.h"
#include "protocol_registry.h"
#include "secplus1.h"

#include "cJSON.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "MQTT_GARAGE";

// ---- command handling (subscriptions) ----

static void dispatch_command(const char *dev, const char *act)
{
  if (strcmp(dev, "door") == 0)
    garage_controller_door_action(act);
  else if (strcmp(dev, "light") == 0)
    garage_controller_light_action(act);
  else if (strcmp(dev, "lock") == 0)
    garage_controller_lock_action(act);
  else
    ESP_LOGW(TAG, "Unknown device: %s", dev);
}

static void handle_command(const char *payload, int payload_len)
{
  char *str = strndup(payload, payload_len);
  if (!str)
    return;

  cJSON *root = cJSON_Parse(str);
  free(str);
  if (!root)
    return;

  cJSON *device = cJSON_GetObjectItem(root, "device");
  cJSON *action = cJSON_GetObjectItem(root, "action");
  if (cJSON_IsString(device) && cJSON_IsString(action))
  {
    dispatch_command(device->valuestring, action->valuestring);
  }

  cJSON_Delete(root);
}

// Handle a plain (non-JSON) payload received on a per-device command topic,
// e.g. "open"/"close"/"stop" on {prefix}/door/set, "on"/"off" on
// {prefix}/light/set, "lock"/"unlock" on {prefix}/lock/set. These are what
// Home Assistant sends through native discovery with no command_template.
static void handle_plain_command(const char *dev, const char *payload,
                                 int payload_len)
{
  if (payload_len <= 0)
    return;
  char *act = strndup(payload, payload_len);
  if (!act)
    return;
  dispatch_command(dev, act);
  free(act);
}

// Legacy single {prefix}/set topic carrying a JSON envelope.
static void on_set_envelope(const char *suffix, const char *data, int len)
{
  (void)suffix;
  handle_command(data, len);
}

static void on_door_set(const char *suffix, const char *data, int len)
{
  (void)suffix;
  handle_plain_command("door", data, len);
}

static void on_light_set(const char *suffix, const char *data, int len)
{
  (void)suffix;
  handle_plain_command("light", data, len);
}

static void on_lock_set(const char *suffix, const char *data, int len)
{
  (void)suffix;
  handle_plain_command("lock", data, len);
}

// ---- state provider ----

static bool provide_state(char *payload, size_t len)
{
  garage_state_t st;
  if (garage_controller_get_state(&st) != ESP_OK)
    return false;

  snprintf(payload, len,
           "{\"protocol\":\"%s\","
           "\"door\":\"%s\",\"moving\":%s,"
           "\"light\":\"%s\",\"locked\":\"%s\","
           "\"obstruction\":%s,\"motion\":%s}",
           protocol_id_str(st.protocol),
           secplus1_door_state_str(st.door_state),
           st.door_moving ? "true" : "false",
           st.light_state == GARAGE_LIGHT_ON ? "on"
               : st.light_state == GARAGE_LIGHT_OFF ? "off" : "unknown",
           st.lock_state == GARAGE_LOCK_LOCKED ? "locked"
               : st.lock_state == GARAGE_LOCK_UNLOCKED ? "unlocked" : "unknown",
           st.obstruction ? "true" : "false",
           st.motion ? "true" : "false");
  return true;
}

// ---- Home Assistant MQTT discovery ----
//
// The state topic carries one JSON document with every value. Commands use
// NATIVE per-device command topics with plain payloads that every HA MQTT
// platform understands without a command_template:
//   door  -> {prefix}/door/set   open|close|stop
//   light -> {prefix}/light/set  on|off
//   lock  -> {prefix}/lock/set   lock|unlock
// (The basic light schema has no command_template, so we must send plain
// on/off - a JSON envelope here would be silently ignored. Hence the native
// topics + plain payloads instead of a JSON envelope.)

static void publish_all_discovery(void)
{
  mqtt_config_t cfg;
  if (mqtt_get_config(&cfg) != ESP_OK || cfg.mode != MQTT_MODE_HA)
    return;
  char topic[128];

  // device descriptor + origin become the entity's device in HA.
  char device[300];
  snprintf(device, sizeof(device),
           "\"device\":{\"identifiers\":[\"sesame_garage\"],"
           "\"name\":\"Sesame Garage\",\"manufacturer\":\"SesameMaker\","
           "\"model\":\"Security+ 1.0 Gateway\"}");

  char buf[1024];

  // Cover (garage door) - plain open/close/stop on {prefix}/door/set
  snprintf(topic, sizeof(topic), "%s/state", cfg.topic_prefix);
  snprintf(buf, sizeof(buf),
           "{%s,\"name\":\"Garage Door\",\"unique_id\":\"sesame_garage_door\","
           "\"device_class\":\"garage\","
           "\"state_topic\":\"%s\","
           "\"value_template\":\"{{ value_json.door }}\","
           "\"state_open\":\"open\",\"state_closed\":\"closed\","
           "\"state_opening\":\"opening\",\"state_closing\":\"closing\","
           "\"state_stopped\":\"stopped\","
           "\"command_topic\":\"%s/door/set\","
           "\"payload_open\":\"open\",\"payload_close\":\"close\","
           "\"payload_stop\":\"stop\",\"qos\":1}",
           device, topic, cfg.topic_prefix);
  mqtt_publish_discovery("cover", "garage_door", buf);

  // Light / lock entities only exist when the active protocol drives them
  // (dry-contact has neither). The state payload above omits them the same
  // way so HA never shows a stale entity.
  protocol_caps_t caps = protocol_registry_caps();
  if (caps.light)
  {
    snprintf(buf, sizeof(buf),
             "{%s,\"name\":\"Garage Light\",\"unique_id\":\"sesame_garage_light\","
             "\"state_topic\":\"%s\","
             "\"state_value_template\":\"{{ value_json.light }}\","
             "\"command_topic\":\"%s/light/set\","
             "\"payload_on\":\"on\",\"payload_off\":\"off\","
             "\"optimistic\":false,\"qos\":1}",
             device, topic, cfg.topic_prefix);
    mqtt_publish_discovery("light", "garage_light", buf);
  }

  // Lock - plain lock/unlock on {prefix}/lock/set
  if (caps.lock)
  {
    snprintf(buf, sizeof(buf),
             "{%s,\"name\":\"Garage Lock\",\"unique_id\":\"sesame_garage_lock\","
             "\"state_topic\":\"%s\","
             "\"value_template\":\"{{ value_json.locked }}\","
             "\"state_locked\":\"locked\",\"state_unlocked\":\"unlocked\","
             "\"command_topic\":\"%s/lock/set\","
             "\"payload_lock\":\"lock\",\"payload_unlock\":\"unlock\","
             "\"qos\":1}",
             device, topic, cfg.topic_prefix);
    mqtt_publish_discovery("lock", "garage_lock", buf);
  }

  // Binary sensor: moving
  snprintf(buf, sizeof(buf),
           "{%s,\"name\":\"Garage Moving\",\"unique_id\":\"sesame_garage_moving\","
           "\"device_class\":\"motion\","
           "\"state_topic\":\"%s\","
           "\"value_template\":\"{{ value_json.moving }}\","
           "\"payload_on\":\"true\",\"payload_off\":\"false\",\"qos\":1}",
           device, topic);
  mqtt_publish_discovery("binary_sensor", "garage_moving", buf);

  // Binary sensor: obstruction only exists on bus protocols; dry-contact
  // reports no obstruction so the entity is skipped.
  if (caps.obstruction)
  {
    snprintf(buf, sizeof(buf),
             "{%s,\"name\":\"Garage Obstruction\","
             "\"unique_id\":\"sesame_garage_obstruction\","
             "\"device_class\":\"problem\","
             "\"state_topic\":\"%s\","
             "\"value_template\":\"{{ value_json.obstruction }}\","
             "\"payload_on\":\"true\",\"payload_off\":\"false\",\"qos\":1}",
             device, topic);
    mqtt_publish_discovery("binary_sensor", "garage_obstruction", buf);
  }
}

void mqtt_garage_register(void)
{
  mqtt_register_topic_handler("set", on_set_envelope);
  mqtt_register_topic_handler("door/set", on_door_set);
  mqtt_register_topic_handler("light/set", on_light_set);
  mqtt_register_topic_handler("lock/set", on_lock_set);
  mqtt_register_state_provider(provide_state);
  mqtt_register_discovery_provider(publish_all_discovery);
}
