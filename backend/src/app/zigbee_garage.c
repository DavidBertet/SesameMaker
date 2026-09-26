// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage-door content for the generic Zigbee service (app).

#include "zigbee_garage.h"

#include "zigbee.h"

#include "garage_controller.h"
#include "protocol_registry.h"
#include "secplus1.h"

#include "esp_log.h"

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
// Device-ID constants for our endpoint table (transport builds the rest).
#include "ezbee/zha.h"
// On/Off cluster + attribute IDs for the generic attr handlers below.
#include "ezbee/zcl/cluster/on_off_desc.h"
#include "ezbee/zcl/zcl_type.h"
#endif

static const char *TAG = "ZIGBEE_GARAGE";

// One endpoint per function so hubs map each to the right entity, all as
// plain switches: door = On/Off Output (on = open), light = On/Off Light,
// remote lockout = On/Off Output (on = remotes disabled), obstruction =
// On/Off Output (on = obstructed, report-only: writes are ignored).
// Light/lock/obstruction only exist when the active protocol drives them
// (dry-contact has none of those). IDs are mirrored in
// tools/sesamemaker-z2m.js (EP) — the parity test in
// tools/sesamemaker-z2m.test.cjs enforces it, change both sides together.
#define ZB_EP_DOOR 10
#define ZB_EP_LIGHT 11
#define ZB_EP_LOCK 12
#define ZB_EP_OBSTRUCTION 13

// Length-prefixed ZCL strings. The length byte is a separate literal:
// "\x0bD..." would parse as \xBD (D is a hex digit), corrupting the attr.
#define ZB_MANUFACTURER_NAME "\x0b" \
                             "DavidBertet"
#define ZB_MODEL_IDENTIFIER "\x0b" \
                            "SesameMaker"

static size_t garage_build_table(zb_endpoint_desc_t *out, size_t max)
{
  protocol_caps_t caps = protocol_registry_caps();
  size_t n = 0;
  if (n < max)
    out[n++] = (zb_endpoint_desc_t){ZB_EP_DOOR, EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID};
  if (caps.light && n < max)
    out[n++] = (zb_endpoint_desc_t){ZB_EP_LIGHT, EZB_ZHA_ON_OFF_LIGHT_DEVICE_ID};
  if (caps.lock && n < max)
    out[n++] = (zb_endpoint_desc_t){ZB_EP_LOCK, EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID};
  if (caps.obstruction && n < max)
    out[n++] = (zb_endpoint_desc_t){ZB_EP_OBSTRUCTION, EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID};
  ESP_LOGI(TAG, "Endpoints: door=10 light=%d lock=%d obstruction=%d", (int)caps.light, (int)caps.lock, (int)caps.obstruction);
  return n;
}

static void garage_on_attr_write(uint8_t ep, uint16_t cluster_id,
                                 uint16_t attr_id, zb_attr_type_t type,
                                 const void *value)
{
  if (cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF ||
      attr_id != EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID || type != ZB_ATTR_BOOL ||
      !value)
    return;
  bool on = *(const bool *)value;
  if (ep == ZB_EP_DOOR)
  {
    ESP_LOGI(TAG, "Door %s", on ? "open" : "close");
    garage_controller_door_action(on ? "open" : "close");
  }
  else if (ep == ZB_EP_LIGHT)
  {
    ESP_LOGI(TAG, "Light %s", on ? "on" : "off");
    garage_controller_light_action(on ? "on" : "off");
  }
  else if (ep == ZB_EP_LOCK)
  {
    ESP_LOGI(TAG, "Remotes %s", on ? "locked out" : "enabled");
    garage_controller_lock_action(on ? "lock" : "unlock");
  }
  else if (ep == ZB_EP_OBSTRUCTION)
  {
    // Report-only sensor: nothing to drive, a write just bounces back on
    // the next report.
    ESP_LOGW(TAG, "Obstruction is report-only, write ignored");
  }
  else
  {
    ESP_LOGW(TAG, "OnOff cmd for unknown ep %u ignored", (unsigned)ep);
  }
}

static void garage_report_bool(uint8_t ep, bool on)
{
  uint8_t val = on ? 1 : 0;
  zb_transport_report_attr(ep, EZB_ZCL_CLUSTER_ID_ON_OFF,
                           EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, ZB_ATTR_U8, &val);
}

static void garage_report_state(void)
{
  garage_state_t st;
  if (garage_controller_get_state(&st) != ESP_OK)
    return;
  // Door as switch state: open = on (1), closed = off (0).
  // Moving/unknown: not reported, the end state reports on arrival
  // (transport dedupes, so repeats are free).
  if (st.door_state == SECPLUS1_DOOR_OPEN)
    garage_report_bool(ZB_EP_DOOR, true);
  else if (st.door_state == SECPLUS1_DOOR_CLOSED)
    garage_report_bool(ZB_EP_DOOR, false);
  if (st.light_state == GARAGE_LIGHT_ON)
    garage_report_bool(ZB_EP_LIGHT, true);
  else if (st.light_state == GARAGE_LIGHT_OFF)
    garage_report_bool(ZB_EP_LIGHT, false);
  if (st.lock_state == GARAGE_LOCK_LOCKED)
    garage_report_bool(ZB_EP_LOCK, true);
  else if (st.lock_state == GARAGE_LOCK_UNLOCKED)
    garage_report_bool(ZB_EP_LOCK, false);
  garage_report_bool(ZB_EP_OBSTRUCTION, st.obstruction);
}

void zigbee_garage_register(void)
{
  static const zb_device_t dev = {
      .build_table = garage_build_table,
      .manufacturer_name = ZB_MANUFACTURER_NAME,
      .model_identifier = ZB_MODEL_IDENTIFIER,
      .on_attr_write = garage_on_attr_write,
      .report_state = garage_report_state,
  };
  zigbee_register_device(&dev);
}
