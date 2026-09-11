// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "zigbee_stack.h"

#include "garage_controller.h"
#include "protocol_registry.h"

#include "esp_log.h"

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED

#include "esp_zigbee_core.h"
#include "esp_partition.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_coexist.h"

static const char *TAG = "ZB_STACK";

// One endpoint per function so hubs map each to the right entity, all as
// plain switches: door = On/Off Output, light = On/Off Light, remote
// lockout = On/Off Output. No lock clusters anywhere (no PIN pad / keypad
// baggage) and no Window Covering (no position/tilt sliders for a binary
// door).
#define ZB_EP_DOOR 10
#define ZB_EP_LIGHT 11
#define ZB_EP_LOCK 12
#define ZB_TASK_STACK 8192
#define ZB_TASK_PRIO 3

// Length-prefixed ZCL strings. The length byte is a separate literal:
// "\x0bD..." would parse as \xBD (D is a hex digit), corrupting the attr.
#define ZB_MANUFACTURER_NAME "\x0b" "DavidBertet"
#define ZB_MODEL_IDENTIFIER "\x0b" "SesameMaker"

static bool s_started = false;
static bool s_ready = false; // esp_zb_start() done, alarms safe

// Last reported values (dedupe; 0xff = never reported).
static uint8_t s_last_door = 0xff; // OnOff: 1 = open, 0 = closed
static uint8_t s_last_onoff = 0xff;
static uint8_t s_last_lock = 0xff;

extern void zigbee_on_joined(uint16_t channel, uint16_t pan_id);
extern void zigbee_on_left(void);
extern bool zigbee_pairing_open(void);

// ---- cross-task entry: scheduler alarms run these in ZB context ----

static void steer_alarm_cb(uint8_t param)
{
  (void)param;
  if (!zigbee_pairing_open())
    return;
  ESP_LOGI(TAG, "Starting steering");
  esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
}

static void leave_alarm_cb(uint8_t param)
{
  (void)param;
  ESP_LOGI(TAG, "Local leave");
  esp_zb_bdb_reset_via_local_action();
}

static void reset_alarm_cb(uint8_t param)
{
  (void)param;
  ESP_LOGI(TAG, "Factory reset");
  esp_zb_factory_reset();
}

// Pending attribute snapshot, applied in ZB context by report_alarm_cb.
static struct
{
  bool has_door;
  uint8_t door;
  bool has_onoff;
  uint8_t onoff;
  bool has_lock;
  uint8_t lock;
} s_pending;

static void report_alarm_cb(uint8_t param)
{
  (void)param;
  if (s_pending.has_door)
  {
    esp_zb_zcl_set_attribute_val(ZB_EP_DOOR, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                 &s_pending.door, false);
    s_pending.has_door = false;
  }
  if (s_pending.has_onoff)
  {
    esp_zb_zcl_set_attribute_val(ZB_EP_LIGHT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &s_pending.onoff, false);
    s_pending.has_onoff = false;
  }
  if (s_pending.has_lock)
  {
    esp_zb_zcl_set_attribute_val(ZB_EP_LOCK, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &s_pending.lock, false);
    s_pending.has_lock = false;
  }
}

// ---- inbound commands ----

static void handle_onoff(const esp_zb_zcl_set_attr_value_message_t *m)
{
  if (m->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF ||
      m->attribute.id != ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID ||
      m->attribute.data.type != ESP_ZB_ZCL_ATTR_TYPE_BOOL || !m->attribute.data.value)
    return;
  bool on = *(bool *)m->attribute.data.value;
  // Door: on = open. Lamp: on = on. Remote lockout: on = remotes disabled.
  if (m->info.dst_endpoint == ZB_EP_DOOR)
  {
    ESP_LOGI(TAG, "Door %s", on ? "open" : "close");
    garage_controller_door_action(on ? "open" : "close");
  }
  else if (m->info.dst_endpoint == ZB_EP_LIGHT)
  {
    ESP_LOGI(TAG, "Light %s", on ? "on" : "off");
    garage_controller_light_action(on ? "on" : "off");
  }
  else if (m->info.dst_endpoint == ZB_EP_LOCK)
  {
    ESP_LOGI(TAG, "Remotes %s", on ? "locked out" : "enabled");
    garage_controller_lock_action(on ? "lock" : "unlock");
  }
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t id, const void *msg)
{
  switch (id)
  {
  case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
    handle_onoff((const esp_zb_zcl_set_attr_value_message_t *)msg);
    break;
  default:
    break;
  }
  return ESP_OK;
}

// ---- commissioning signals ----

void esp_zb_app_signal_handler(esp_zb_app_signal_t *s)
{
  esp_zb_app_signal_type_t sig = *s->p_app_signal;
  switch (sig)
  {
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    break;
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    if (s->esp_err_status != ESP_OK)
    {
      ESP_LOGW(TAG, "Stack init failed: %s", esp_err_to_name(s->esp_err_status));
      break;
    }
    if (esp_zb_bdb_is_factory_new())
    {
      ESP_LOGI(TAG, "Factory new");
      if (zigbee_pairing_open())
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
    }
    else
    {
      ESP_LOGI(TAG, "Rejoined (ch %d, pan 0x%04x)", esp_zb_get_current_channel(), esp_zb_get_pan_id());
      zigbee_on_joined(esp_zb_get_current_channel(), esp_zb_get_pan_id());
    }
    break;
  case ESP_ZB_BDB_SIGNAL_STEERING:
    if (s->esp_err_status == ESP_OK)
    {
      ESP_LOGI(TAG, "Joined (ch %d, pan 0x%04x)", esp_zb_get_current_channel(), esp_zb_get_pan_id());
      zigbee_on_joined(esp_zb_get_current_channel(), esp_zb_get_pan_id());
    }
    else if (zigbee_pairing_open())
    {
      ESP_LOGI(TAG, "Steering failed, retry");
      esp_zb_scheduler_alarm(steer_alarm_cb, 0, 1000); // retry while window open
    }
    else
    {
      ESP_LOGI(TAG, "Steering stopped");
    }
    break;
  default:
    break;
  }
}

// ---- endpoint + task ----

// Basic + Identify shared by every endpoint (hubs need both to interview
// an endpoint as a standalone device).
static void add_basic_identify(esp_zb_cluster_list_t *clusters)
{
  esp_zb_basic_cluster_cfg_t basic_cfg = {
      .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
      .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
  };
  esp_zb_identify_cluster_cfg_t identify_cfg = {
      .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
  };
  esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
  esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                (void *)ZB_MANUFACTURER_NAME);
  esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                (void *)ZB_MODEL_IDENTIFIER);
  esp_zb_cluster_list_add_basic_cluster(clusters, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(clusters, esp_zb_identify_cluster_create(&identify_cfg),
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

static esp_zb_cluster_list_t *output_clusters(void)
{
  esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
  add_basic_identify(clusters);
  esp_zb_on_off_cluster_cfg_t onoff_cfg = {
      .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
  };
  esp_zb_cluster_list_add_on_off_cluster(clusters, esp_zb_on_off_cluster_create(&onoff_cfg),
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  return clusters;
}

static void add_endpoint(esp_zb_ep_list_t *ep_list, esp_zb_cluster_list_t *clusters,
                         uint8_t endpoint, uint16_t device_id)
{
  esp_zb_endpoint_config_t ep_cfg = {
      .endpoint = endpoint,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = device_id,
      .app_device_version = 0,
  };
  esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg);
}

static esp_zb_ep_list_t *build_endpoints(void)
{
  protocol_caps_t caps = protocol_registry_caps();
  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

  // Door always present: On/Off Output where on = open.
  add_endpoint(ep_list, output_clusters(), ZB_EP_DOOR, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);

  if (caps.light)
  {
    add_endpoint(ep_list, output_clusters(), ZB_EP_LIGHT, ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID);
  }
  if (caps.lock)
  {
    // Remote lockout as a switch too (on = remotes disabled).
    add_endpoint(ep_list, output_clusters(), ZB_EP_LOCK, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
  }
  return ep_list;
}

static void zb_task(void *arg)
{
  (void)arg;
  static bool platform_done = false;
  if (!platform_done)
  {
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));
    platform_done = true;
  }

  // Coex arbiter BEFORE esp_zb_start: once 802.15.4 RX owns the radio,
  // Wi-Fi loses receive windows and all traffic dies (beacon timeouts,
  // reason-200 drops, dead web UI). Wi-Fi is already up (wifi-first boot),
  // so pre-start is the earliest possible point.
  ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());

  esp_zb_cfg_t zb_cfg = {
      .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
      .install_code_policy = false,
      .nwk_cfg.zed_cfg = {
          .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
          .keep_alive = 3000,
      },
  };
  esp_zb_init(&zb_cfg);
  esp_zb_device_register(build_endpoints());
  esp_zb_core_action_handler_register(zb_action_handler);
  ESP_ERROR_CHECK(esp_zb_start(false));
  s_ready = true;

  esp_zb_stack_main_loop();
  vTaskDelete(NULL);
}

// ---- public API ----

void zigbee_stack_start(void)
{
  if (s_started)
    return;
  s_started = true;
  xTaskCreate(zb_task, "zb_main", ZB_TASK_STACK, NULL, ZB_TASK_PRIO, NULL);
}

void zigbee_stack_pair(void)
{
  if (!s_ready)
  {
    ESP_LOGW(TAG, "Stack not ready, steering skipped");
    return;
  }
  esp_zb_scheduler_alarm(steer_alarm_cb, 0, 100);
}

void zigbee_stack_leave(void)
{
  if (!s_ready)
    return;
  esp_zb_scheduler_alarm(leave_alarm_cb, 0, 100);
}

void zigbee_stack_factory_reset(void)
{
  if (!s_ready)
    return;
  esp_zb_scheduler_alarm(reset_alarm_cb, 0, 100);
}

void zigbee_stack_report(const garage_state_t *st)
{
  if (!s_ready)
    return;
  bool kick = false;
  // Door as switch state: open = on (1), closed = off (0).
  // Moving/unknown: keep last, the end state reports on arrival.
  uint8_t door = s_last_door;
  switch (st->door_state)
  {
  case SECPLUS1_DOOR_OPEN:
    door = 1;
    break;
  case SECPLUS1_DOOR_CLOSED:
    door = 0;
    break;
  default:
    break;
  }
  if (door != s_last_door && door <= 1)
  {
    s_last_door = door;
    s_pending.door = door;
    s_pending.has_door = true;
    kick = true;
  }
  if (st->light_state == GARAGE_LIGHT_ON || st->light_state == GARAGE_LIGHT_OFF)
  {
    uint8_t on = (st->light_state == GARAGE_LIGHT_ON) ? 1 : 0;
    if (on != s_last_onoff)
    {
      s_last_onoff = on;
      s_pending.onoff = on;
      s_pending.has_onoff = true;
      kick = true;
    }
  }
  if (st->lock_state == GARAGE_LOCK_LOCKED || st->lock_state == GARAGE_LOCK_UNLOCKED)
  {
    uint8_t on = (st->lock_state == GARAGE_LOCK_LOCKED) ? 1 : 0;
    if (on != s_last_lock)
    {
      s_last_lock = on;
      s_pending.lock = on;
      s_pending.has_lock = true;
      kick = true;
    }
  }
  if (kick)
    esp_zb_scheduler_alarm(report_alarm_cb, 0, 100);
}

#else // !CONFIG_SOC_IEEE802154_SUPPORTED

void zigbee_stack_start(void) {}
void zigbee_stack_pair(void) {}
void zigbee_stack_leave(void) {}
void zigbee_stack_factory_reset(void) {}
void zigbee_stack_report(const garage_state_t *st) { (void)st; }

#endif
