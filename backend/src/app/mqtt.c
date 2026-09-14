// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "mqtt.h"

#include "garage_controller.h"
#include "protocol_registry.h"
#include "secplus1.h"
#include "storage.h"

#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "MQTT";

#define MQTT_CFG_KEY "mqtt_cfg"
#define MQTT_QOS 1

static mqtt_config_t s_config = {
    .mode = MQTT_MODE_OFF,
    .uri = "",
    .username = "",
    .password = "",
    .topic_prefix = "home/sesame",
};

static esp_mqtt_client_handle_t s_client = NULL;

static void publish_all_discovery(void);

static char *make_topic(const char *suffix, char *buf, size_t len)
{
  snprintf(buf, len, "%s/%s", s_config.topic_prefix, suffix);
  return buf;
}

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

// ---- MQTT event handler ----

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Connected to broker");
    {
      char topic[128];
      make_topic("set", topic, sizeof(topic));
      esp_mqtt_client_subscribe(s_client, topic, MQTT_QOS);
      ESP_LOGI(TAG, "Subscribed to %s", topic);
    }
    {
      // Per-device plain-command topics used by HA discovery.
      const char *devs[] = { "door", "light", "lock" };
      for (size_t i = 0; i < sizeof(devs) / sizeof(devs[0]); i++)
      {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/%s/set", s_config.topic_prefix,
                 devs[i]);
        esp_mqtt_client_subscribe(s_client, topic, MQTT_QOS);
        ESP_LOGI(TAG, "Subscribed to %s", topic);
      }
    }
    // Publish current state on connect so HA discovers the device immediately
    mqtt_publish_garage_state();
    // Send HA discovery configs once so entities are auto-created.
    publish_all_discovery();
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Disconnected from broker");
    break;
  case MQTT_EVENT_DATA:
    if (event->topic_len <= 0)
      break;
    {
      const char *pre = s_config.topic_prefix;
      size_t plen = strlen(pre);
      if (event->topic_len == (int)(plen + 4) &&
          strncmp(event->topic, pre, plen) == 0 &&
          strncmp(event->topic + plen, "/set", 4) == 0)
      {
        // Legacy single {prefix}/set topic carrying a JSON envelope.
        handle_command(event->data, event->data_len);
      }
      else if (event->topic_len == (int)(plen + 9) &&
               strncmp(event->topic, pre, plen) == 0 &&
               strncmp(event->topic + plen + 1, "door/set", 8) == 0)
      {
        handle_plain_command("door", event->data, event->data_len);
      }
      else if (event->topic_len == (int)(plen + 10) &&
               strncmp(event->topic, pre, plen) == 0 &&
               strncmp(event->topic + plen + 1, "light/set", 9) == 0)
      {
        handle_plain_command("light", event->data, event->data_len);
      }
      else if (event->topic_len == (int)(plen + 9) &&
               strncmp(event->topic, pre, plen) == 0 &&
               strncmp(event->topic + plen + 1, "lock/set", 8) == 0)
      {
        handle_plain_command("lock", event->data, event->data_len);
      }
    }
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT error");
    break;
  default:
    break;
  }
}

// ---- config persistence ----

static esp_err_t persist_config(const mqtt_config_t *cfg)
{
  return write_blob(MQTT_CFG_KEY, cfg, sizeof(mqtt_config_t));
}

static esp_err_t load_config(void)
{
  size_t stored_size = 0;
  esp_err_t ret = read_blob(MQTT_CFG_KEY, NULL, &stored_size);
  if (ret == ESP_OK && stored_size == sizeof(s_config))
  {
    return read_blob(MQTT_CFG_KEY, &s_config, &stored_size);
  }
  if (ret != ESP_OK)
  {
    return persist_config(&s_config);
  }

  // Migrate an older persisted struct into the current one.
  memset(&s_config, 0, sizeof(s_config));
  snprintf(s_config.topic_prefix, sizeof(s_config.topic_prefix), "home/sesame");

  // Oldest layout: bool enabled + uri + username + password + topic_prefix.
  // "enabled" and the new "mode" int occupy the same 4 bytes, and old
  // enabled==true becomes MASTER automatically (1==MQTT_MODE_MASTER).
  size_t legacy_size = offsetof(mqtt_config_t, topic_prefix) + 64;
  if (stored_size == legacy_size)
  {
    ret = read_blob(MQTT_CFG_KEY, &s_config, &stored_size);
    // Fix up: a custom incoming mode value that isn't a valid enum -> OFF.
    if (s_config.mode != MQTT_MODE_MASTER)
      s_config.mode = MQTT_MODE_OFF;
    return persist_config(&s_config);
  }

  return persist_config(&s_config);
}

// ---- client lifecycle ----

static void stop_client(void)
{
  if (s_client)
  {
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
  }
}

static void start_client(void)
{
  stop_client();

  if (s_config.mode == MQTT_MODE_OFF || strlen(s_config.uri) == 0)
  {
    ESP_LOGI(TAG, "MQTT disabled");
    return;
  }

  esp_mqtt_client_config_t mqtt_cfg;
  memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
  mqtt_cfg.broker.address.uri = s_config.uri;
  if (strlen(s_config.username) > 0)
  {
    mqtt_cfg.credentials.username = s_config.username;
    mqtt_cfg.credentials.authentication.password = s_config.password;
  }

  s_client = esp_mqtt_client_init(&mqtt_cfg);
  if (!s_client)
  {
    ESP_LOGE(TAG, "Failed to init MQTT client");
    return;
  }
  esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
  esp_mqtt_client_start(s_client);
  ESP_LOGI(TAG, "MQTT client starting for %s", s_config.uri);
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

static const char *HA_DISCOVERY_PREFIX = "homeassistant";

static void publish_discovery(const char *component, const char *object_id,
                              const char *json_config)
{
  if (s_config.mode == MQTT_MODE_OFF || !s_client)
    return;
  char topic[256];
  snprintf(topic, sizeof(topic), "%s/%s/%s/config", HA_DISCOVERY_PREFIX,
           component, object_id);
  esp_mqtt_client_publish(s_client, topic, json_config, 0, MQTT_QOS, 1);
}

static void publish_all_discovery(void)
{
  if (s_config.mode != MQTT_MODE_HA)
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
  make_topic("state", topic, sizeof(topic));  snprintf(buf, sizeof(buf),
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
           device, topic, s_config.topic_prefix);
  publish_discovery("cover", "garage_door", buf);

  // Light / lock entities only exist when the active protocol drives them
  // (dry-contact has neither). The state payload below omits them the same
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
           device, topic, s_config.topic_prefix);
    publish_discovery("light", "garage_light", buf);
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
             device, topic, s_config.topic_prefix);
    publish_discovery("lock", "garage_lock", buf);
  }

  // Binary sensor: moving
  snprintf(buf, sizeof(buf),
           "{%s,\"name\":\"Garage Moving\",\"unique_id\":\"sesame_garage_moving\","
           "\"device_class\":\"motion\","
           "\"state_topic\":\"%s\","
           "\"value_template\":\"{{ value_json.moving }}\","
            "\"payload_on\":\"true\",\"payload_off\":\"false\",\"qos\":1}",
            device, topic);
  publish_discovery("binary_sensor", "garage_moving", buf);

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
    publish_discovery("binary_sensor", "garage_obstruction", buf);
  }
}

// ---- public API ----

esp_err_t mqtt_init(void)
{
  esp_err_t ret = load_config();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to load MQTT config");
    return ret;
  }
  start_client();
  return ESP_OK;
}

esp_err_t mqtt_get_config(mqtt_config_t *out)
{
  if (!out)
    return ESP_ERR_INVALID_ARG;
  *out = s_config;
  return ESP_OK;
}

esp_err_t mqtt_set_config(const mqtt_config_t *cfg)
{
  if (!cfg)
    return ESP_ERR_INVALID_ARG;
  if (cfg->mode != MQTT_MODE_OFF && strlen(cfg->uri) == 0)
  {
    ESP_LOGW(TAG, "Enabled MQTT requires a broker URI");
    return ESP_ERR_INVALID_ARG;
  }

  mqtt_config_t merged = *cfg;
  if (strlen(merged.password) == 0 && strlen(s_config.password) > 0)
  {
    memcpy(merged.password, s_config.password, sizeof(merged.password));
  }

  esp_err_t ret = persist_config(&merged);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to persist MQTT config");
    return ret;
  }

  s_config = merged;
  start_client();
  ESP_LOGI(TAG, "MQTT config updated");
  return ESP_OK;
}

// Only publish when the state actually changed from the last publish, so we
// don't hammer the broker with identical retained frames every second.
static char s_last_state_payload[384] = "";

void mqtt_publish_garage_state(void)
{
  if (s_config.mode == MQTT_MODE_OFF || !s_client)
    return;

  garage_state_t st;
  if (garage_controller_get_state(&st) != ESP_OK)
    return;

  char topic[128];
  char payload[384];

  make_topic("state", topic, sizeof(topic));
  snprintf(payload, sizeof(payload),
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

  if (strcmp(payload, s_last_state_payload) == 0)
    return;

  memcpy(s_last_state_payload, payload, sizeof(s_last_state_payload));
  esp_mqtt_client_publish(s_client, topic, payload, 0, MQTT_QOS, 1);
}
