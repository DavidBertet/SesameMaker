// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic HomeKit transport (app).

#include "homekit_garage.h"

#include "homekit.h"

#include "garage_controller.h"

#include "hap.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "esp_log.h"
#include "esp_wifi.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "HK_GARAGE";

// Apple HAP spec values (uint8 characteristics).
#define HK_DOOR_OPEN 0
#define HK_DOOR_CLOSED 1
#define HK_DOOR_OPENING 2
#define HK_DOOR_CLOSING 3
#define HK_DOOR_STOPPED 4
#define HK_LOCK_UNSECURED 0
#define HK_LOCK_SECURED 1

// Characteristic handles for state pushes (grabbed at build time).
static hap_char_t *s_door_current = NULL;
static hap_char_t *s_door_target = NULL;
static hap_char_t *s_obstruction = NULL;
static hap_char_t *s_light_on = NULL;
static hap_char_t *s_lock_current = NULL;
static hap_char_t *s_lock_target = NULL;
static hap_char_t *s_motion = NULL;

// Last commanded direction (Home app + local toggles both funnel here
// via report_state): lets a moving door report Opening vs Closing.
static uint8_t s_last_target = HK_DOOR_CLOSED;
// Last pushed values (HAP notifies on every update_val, so skip repeats).
static uint8_t s_last_door = 0xff;
static uint8_t s_last_obstr = 0xff;
static uint8_t s_last_light = 0xff;
static uint8_t s_last_lock = 0xff;
static uint8_t s_last_motion = 0xff;

static void push_u8(hap_char_t *hc, uint8_t *last, uint8_t val)
{
  if (!hc || (last && *last == val))
    return;
  hap_val_t v = {.u = val};
  hap_char_update_val(hc, &v);
  if (last)
    *last = val;
}

static void push_bool(hap_char_t *hc, uint8_t *last, bool val)
{
  push_u8(hc, last, val ? 1 : 0);
}

// ---- inbound writes (Home app -> device) ----

static int door_write(hap_write_data_t write_data[], int count,
                      void *serv_priv, void *write_priv)
{
  (void)serv_priv;
  (void)write_priv;
  for (int i = 0; i < count; i++)
  {
    if (strcmp(hap_char_get_type_uuid(write_data[i].hc),
               HAP_CHAR_UUID_TARGET_DOOR_STATE) != 0)
      continue;
    uint8_t target = (uint8_t)write_data[i].val.u;
    s_last_target = (target == HK_DOOR_OPEN) ? HK_DOOR_OPEN : HK_DOOR_CLOSED;
    ESP_LOGI(TAG, "Door %s", target == HK_DOOR_OPEN ? "open" : "close");
    garage_controller_door_action(target == HK_DOOR_OPEN ? "open" : "close");
    *write_data[i].status = HAP_STATUS_SUCCESS;
  }
  return HAP_SUCCESS;
}

static int light_write(hap_write_data_t write_data[], int count,
                       void *serv_priv, void *write_priv)
{
  (void)serv_priv;
  (void)write_priv;
  for (int i = 0; i < count; i++)
  {
    if (strcmp(hap_char_get_type_uuid(write_data[i].hc), HAP_CHAR_UUID_ON) != 0)
      continue;
    bool on = write_data[i].val.b;
    ESP_LOGI(TAG, "Light %s", on ? "on" : "off");
    garage_controller_light_action(on ? "on" : "off");
    *write_data[i].status = HAP_STATUS_SUCCESS;
  }
  return HAP_SUCCESS;
}

static int lock_write(hap_write_data_t write_data[], int count,
                      void *serv_priv, void *write_priv)
{
  (void)serv_priv;
  (void)write_priv;
  for (int i = 0; i < count; i++)
  {
    if (strcmp(hap_char_get_type_uuid(write_data[i].hc),
               HAP_CHAR_UUID_LOCK_TARGET_STATE) != 0)
      continue;
    bool lock = write_data[i].val.u == HK_LOCK_SECURED;
    ESP_LOGI(TAG, "Remotes %s", lock ? "locked out" : "enabled");
    garage_controller_lock_action(lock ? "lock" : "unlock");
    *write_data[i].status = HAP_STATUS_SUCCESS;
  }
  return HAP_SUCCESS;
}

// ---- accessory definition ----

static int identify_routine(hap_acc_t *ha)
{
  (void)ha;
  // No status LED on this board: identify is a log line. The Home app
  // still confirms the round trip.
  ESP_LOGW(TAG, "Identify requested");
  return HAP_SUCCESS;
}

static void build_accessory(void)
{
  hap_acc_cfg_t cfg = {
      .name = "SesameMaker",
      .manufacturer = "DavidBertet",
      .model = "SesameMaker",
      .serial_num = "SM0001",
      .fw_rev = "1.0.0",
      .hw_rev = "1.0",
      .pv = "1.1",
      .identify_routine = identify_routine,
      .cid = HAP_CID_GARAGE_DOOR_OPENER,
  };
  // Stable per-device serial from the STA MAC (two SesameMakers in one
  // home stay distinguishable).
  static char serial[13];
  uint8_t mac[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
  {
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5]);
    cfg.serial_num = serial;
  }

  hap_acc_t *acc = hap_acc_create(&cfg);
  if (!acc)
  {
    ESP_LOGE(TAG, "hap_acc_create failed");
    return;
  }
  uint8_t product_data[] = {'S', 'E', 'S', 'A', 'M', 'E'};
  hap_acc_add_product_data(acc, product_data, sizeof(product_data));
  hap_acc_add_wifi_transport_service(acc, 0);

  hap_serv_t *door = hap_serv_garage_door_opener_create(HK_DOOR_CLOSED,
                                                        HK_DOOR_CLOSED, false);
  hap_serv_t *light = hap_serv_lightbulb_create(false);
  hap_serv_t *lock = hap_serv_lock_mechanism_create(HK_LOCK_UNSECURED,
                                                    HK_LOCK_UNSECURED);
  hap_serv_t *motion = hap_serv_motion_sensor_create(false);
  if (!door || !light || !lock || !motion)
  {
    ESP_LOGE(TAG, "service create failed");
    return;
  }
  hap_serv_set_write_cb(door, door_write);
  hap_serv_set_write_cb(light, light_write);
  hap_serv_set_write_cb(lock, lock_write);
  hap_acc_add_serv(acc, door);
  hap_acc_add_serv(acc, light);
  hap_acc_add_serv(acc, lock);
  hap_acc_add_serv(acc, motion);

  s_door_current = hap_serv_get_char_by_uuid(door, HAP_CHAR_UUID_CURRENT_DOOR_STATE);
  s_door_target = hap_serv_get_char_by_uuid(door, HAP_CHAR_UUID_TARGET_DOOR_STATE);
  s_obstruction = hap_serv_get_char_by_uuid(door, HAP_CHAR_UUID_OBSTRUCTION_DETECTED);
  s_light_on = hap_serv_get_char_by_uuid(light, HAP_CHAR_UUID_ON);
  s_lock_current = hap_serv_get_char_by_uuid(lock, HAP_CHAR_UUID_LOCK_CURRENT_STATE);
  s_lock_target = hap_serv_get_char_by_uuid(lock, HAP_CHAR_UUID_LOCK_TARGET_STATE);
  s_motion = hap_serv_get_char_by_uuid(motion, HAP_CHAR_UUID_MOTION_DETECTED);

  hap_add_accessory(acc);
  ESP_LOGI(TAG, "Accessory built");
}

// ---- outbound state ----

void homekit_report_state(void)
{
  if (!homekit_started())
    return;
  garage_state_t st;
  if (garage_controller_get_state(&st) != ESP_OK)
    return;

  uint8_t door;
  if (st.door_state == SECPLUS1_DOOR_OPEN)
    door = HK_DOOR_OPEN;
  else if (st.door_state == SECPLUS1_DOOR_CLOSED)
    door = HK_DOOR_CLOSED;
  else if (st.door_moving)
    door = (s_last_target == HK_DOOR_OPEN) ? HK_DOOR_OPENING : HK_DOOR_CLOSING;
  else
    door = HK_DOOR_STOPPED;
  if (door == HK_DOOR_OPEN || door == HK_DOOR_CLOSED)
  {
    // End states become the new direction reference.
    s_last_target = door;
    push_u8(s_door_target, NULL, door);
  }
  push_u8(s_door_current, &s_last_door, door);
  push_bool(s_obstruction, &s_last_obstr, st.obstruction);

  if (st.light_state == GARAGE_LIGHT_ON)
    push_bool(s_light_on, &s_last_light, true);
  else if (st.light_state == GARAGE_LIGHT_OFF)
    push_bool(s_light_on, &s_last_light, false);

  if (st.lock_state == GARAGE_LOCK_LOCKED)
  {
    push_u8(s_lock_current, &s_last_lock, HK_LOCK_SECURED);
    push_u8(s_lock_target, NULL, HK_LOCK_SECURED);
  }
  else if (st.lock_state == GARAGE_LOCK_UNLOCKED)
  {
    push_u8(s_lock_current, &s_last_lock, HK_LOCK_UNSECURED);
    push_u8(s_lock_target, NULL, HK_LOCK_UNSECURED);
  }

  push_bool(s_motion, &s_last_motion, st.motion);
}

void homekit_garage_register(void)
{
  static const homekit_device_t dev = {
      .build_accessory = build_accessory,
      .category_id = HAP_CID_GARAGE_DOOR_OPENER,
  };
  homekit_register_device(&dev);
}
