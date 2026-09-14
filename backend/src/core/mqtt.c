// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Generic MQTT bridge transport (core, no device knowledge).

#include "mqtt.h"

#include "storage.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "MQTT";

#define MQTT_CFG_KEY "mqtt_cfg"
#define MQTT_QOS 1
#define MQTT_STATE_BUF 512
#define MQTT_MAX_TOPIC_HANDLERS 8
#define MQTT_MAX_SUFFIX_LEN 48
// Stringified precision bound for topic snprintf: registered suffixes are
// capped at MQTT_MAX_SUFFIX_LEN, which lets -Wformat-truncation prove the
// 128B topic buffers below can never overflow.
#define MQTT_STR_(x) #x
#define MQTT_STR(x) MQTT_STR_(x)

static mqtt_config_t s_config = {
    .mode = MQTT_MODE_OFF,
    .uri = "",
    .username = "",
    .password = "",
    .topic_prefix = "home/sesame",
};

static esp_mqtt_client_handle_t s_client = NULL;

typedef struct
{
  char suffix[MQTT_MAX_SUFFIX_LEN + 1];
  mqtt_topic_cb_t cb;
} topic_handler_t;

static topic_handler_t s_topic_handlers[MQTT_MAX_TOPIC_HANDLERS];
static size_t s_topic_handler_count = 0;
static mqtt_state_provider_t s_state_provider = NULL;
static mqtt_discovery_provider_t s_discovery_provider = NULL;
// Last published state payload: identical frames are skipped so we don't
// hammer the broker with retained duplicates.
static char s_last_state_payload[MQTT_STATE_BUF] = "";

static char *make_topic(const char *suffix, char *buf, size_t len)
{
  snprintf(buf, len, "%s/%." MQTT_STR(MQTT_MAX_SUFFIX_LEN) "s",
           s_config.topic_prefix, suffix);
  return buf;
}

bool mqtt_register_topic_handler(const char *suffix, mqtt_topic_cb_t cb)
{
  if (!suffix || !cb || strlen(suffix) == 0 ||
      strlen(suffix) > MQTT_MAX_SUFFIX_LEN)
    return false;
  for (size_t i = 0; i < s_topic_handler_count; i++)
  {
    if (strcmp(s_topic_handlers[i].suffix, suffix) == 0)
      return false;
  }
  if (s_topic_handler_count >= MQTT_MAX_TOPIC_HANDLERS)
    return false;
  snprintf(s_topic_handlers[s_topic_handler_count].suffix,
           sizeof(s_topic_handlers[s_topic_handler_count].suffix), "%s",
           suffix);
  s_topic_handlers[s_topic_handler_count].cb = cb;
  s_topic_handler_count++;
  return true;
}

void mqtt_register_state_provider(mqtt_state_provider_t fn)
{
  s_state_provider = fn;
}

void mqtt_register_discovery_provider(mqtt_discovery_provider_t fn)
{
  s_discovery_provider = fn;
}

// ---- MQTT event handler ----

static void subscribe_all(void)
{
  for (size_t i = 0; i < s_topic_handler_count; i++)
  {
    char topic[128];
    make_topic(s_topic_handlers[i].suffix, topic, sizeof(topic));
    esp_mqtt_client_subscribe(s_client, topic, MQTT_QOS);
    ESP_LOGI(TAG, "Subscribed to %s", topic);
  }
}

static void route_data(const char *topic, int topic_len, const char *data,
                       int data_len)
{
  if (topic_len <= 0)
    return;
  const char *pre = s_config.topic_prefix;
  size_t plen = strlen(pre);
  // Must be exactly "{prefix}/{suffix}".
  if (topic_len <= (int)plen + 1 || strncmp(topic, pre, plen) != 0 ||
      topic[plen] != '/')
    return;
  const char *suffix = topic + plen + 1;
  size_t suffix_len = (size_t)topic_len - plen - 1;
  for (size_t i = 0; i < s_topic_handler_count; i++)
  {
    if (strlen(s_topic_handlers[i].suffix) == suffix_len &&
        strncmp(s_topic_handlers[i].suffix, suffix, suffix_len) == 0)
    {
      s_topic_handlers[i].cb(s_topic_handlers[i].suffix, data, data_len);
      return;
    }
  }
  ESP_LOGW(TAG, "No handler for topic");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Connected to broker");
    subscribe_all();
    // Publish current state on connect so subscribers (HA discovery)
    // see the device immediately.
    mqtt_notify_changed();
    // Send HA discovery configs once so entities are auto-created.
    if (s_config.mode == MQTT_MODE_HA && s_discovery_provider)
      s_discovery_provider();
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Disconnected from broker");
    break;
  case MQTT_EVENT_DATA:
    route_data(event->topic, event->topic_len, event->data, event->data_len);
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

esp_err_t mqtt_publish(const char *suffix, const char *payload, int qos,
                       bool retain)
{
  if (s_config.mode == MQTT_MODE_OFF || !s_client || !suffix || !payload)
    return ESP_ERR_INVALID_STATE;
  char topic[128];
  make_topic(suffix, topic, sizeof(topic));
  return esp_mqtt_client_publish(s_client, topic, payload, 0, qos,
                                 retain ? 1 : 0);
}

void mqtt_publish_discovery(const char *component, const char *object_id,
                            const char *json_config)
{
  if (s_config.mode == MQTT_MODE_OFF || !s_client)
    return;
  char topic[256];
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config", component,
           object_id);
  esp_mqtt_client_publish(s_client, topic, json_config, 0, MQTT_QOS, 1);
}

void mqtt_notify_changed(void)
{
  if (s_config.mode == MQTT_MODE_OFF || !s_client || !s_state_provider)
    return;
  char payload[MQTT_STATE_BUF];
  if (!s_state_provider(payload, sizeof(payload)))
    return;
  if (strcmp(payload, s_last_state_payload) == 0)
    return;
  snprintf(s_last_state_payload, sizeof(s_last_state_payload), "%s", payload);
  char topic[128];
  make_topic("state", topic, sizeof(topic));
  esp_mqtt_client_publish(s_client, topic, payload, 0, MQTT_QOS, 1);
}
