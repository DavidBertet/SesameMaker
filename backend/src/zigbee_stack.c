// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "zigbee_stack.h"

#include "garage_controller.h"
#include "protocol_registry.h"
#include "wifi.h"
#include "zigbee_bdb.h"
#include "zigbee_lqi.h"
#include "zigbee_rejoin.h"

#include "esp_log.h"

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED

#include "esp_zigbee_core.h"
#include "esp_partition.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nwk/esp_zigbee_nwk.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "esp_coexist.h"

// zigbee_lqi.h compares relationships numerically so host tests need no IDF
// headers; the firmware build pins the value to the real enum here.
_Static_assert(ESP_ZB_NWK_RELATIONSHIP_PARENT == ZB_LQI_PARENT_RELATIONSHIP,
               "parent relationship mismatch");

// Same deal for zigbee_bdb.h: BDB commissioning statuses are stable across
// esp-zigbee-lib 1.x/2.x, pinned here so a renumber breaks the build.
_Static_assert(ESP_ZB_BDB_STATUS_SUCCESS == 0, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_IN_PROGRESS == 1, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NOT_AA_CAPABLE == 2, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NO_NETWORK == 3, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_TARGET_FAILURE == 4, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_FORMATION_FAILURE == 5, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NO_IDENTIFY_QUERY_RESPONSE == 6, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_BINDING_TABLE_FULL == 7, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NO_SCAN_RESPONSE == 8, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NOT_PERMITTED == 9, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_TCLK_EX_FAILURE == 10, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_NOT_ON_A_NETWORK == 11, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_ON_A_NETWORK == 12, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_CANCELLED == 13, "bdb status mismatch");
_Static_assert(ESP_ZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE == 14, "bdb status mismatch");

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
// 802.15.4 TX power in dBm (C6 range -15..+20). Max: the garage is often far
// from the coordinator/routers and this is a mains-powered device.
#define ZB_TX_POWER_DBM 20
// Max time to let Wi-Fi associate before starting the 802.15.4 radio.
// Applies only when STA credentials exist; pure-AP boots skip the wait.
// Polls is_wifi_connected(); starts early on GOT_IP, starts anyway at the
// cap so a down AP never blocks Zigbee.
#define ZB_WIFI_WAIT_MAX_MS 15000
#define ZB_WIFI_WAIT_POLL_MS 500

#define ZB_SCAN_DURATION 4

// Length-prefixed ZCL strings. The length byte is a separate literal:
// "\x0bD..." would parse as \xBD (D is a hex digit), corrupting the attr.
#define ZB_MANUFACTURER_NAME "\x0b" \
                             "DavidBertet"
#define ZB_MODEL_IDENTIFIER "\x0b" \
                            "SesameMaker"

static bool s_started = false;
static bool s_ready = false; // esp_zb_start() done, alarms safe

// Last reported values (dedupe; 0xff = never reported).
static uint8_t s_last_door = 0xff; // OnOff: 1 = open, 0 = closed
static uint8_t s_last_onoff = 0xff;
static uint8_t s_last_lock = 0xff;

extern void zigbee_on_joined(uint16_t channel, uint16_t pan_id);
extern void zigbee_on_left(void);
extern void zigbee_on_commissioned(bool commissioned);
extern void zigbee_on_parent(uint16_t addr, uint8_t depth, uint8_t lqi);
extern bool zigbee_pairing_open(void);

// ---- parent-link LQI poll (Mgmt_Lqi_req to self, ~1/min while joined) ----

static bool s_lqi_polling = false;

static void lqi_rsp_cb(const esp_zb_zdo_mgmt_lqi_rsp_t *rsp, void *ctx)
{
  (void)ctx;
  if (!rsp || rsp->status != 0 || !rsp->neighbor_table_list)
  {
    ESP_LOGD(TAG, "LQI rsp missing (status=%d)", rsp ? (int)rsp->status : -1);
    return;
  }
  uint8_t count = rsp->neighbor_table_list_count;
  if (count > ZB_LQI_MAX_ENTRIES)
  {
    ESP_LOGI(TAG, "LQI rsp truncated: %u entries, scanning first %u", count, ZB_LQI_MAX_ENTRIES);
    count = ZB_LQI_MAX_ENTRIES;
  }
  int lqi = -1;
  uint16_t parent_addr = 0;
  uint8_t parent_depth = 0;
  for (uint8_t i = 0; i < count; i++)
  {
    const esp_zb_zdo_neighbor_table_list_record_t *n =
        &rsp->neighbor_table_list[i];
    if (n->relationship == ESP_ZB_NWK_RELATIONSHIP_PARENT)
    {
      lqi = n->lqi;
      parent_addr = n->network_addr;
      parent_depth = n->depth;
      break;
    }
  }
  if (lqi < 0)
  {
    ESP_LOGI(TAG, "LQI poll: no parent entry in %u neighbors", rsp->neighbor_table_list_count);
    return;
  }
  ESP_LOGD(TAG, "LQI poll: parent 0x%04x depth %u lqi=%d", parent_addr, parent_depth, lqi);
  zigbee_on_parent(parent_addr, parent_depth, (uint8_t)lqi);
}

static void lqi_alarm_cb(uint8_t param)
{
  (void)param;
  if (!s_ready || !esp_zb_bdb_dev_joined())
  {
    s_lqi_polling = false;
    return;
  }
  esp_zb_zdo_mgmt_lqi_req_param_t req = {
      .start_index = 0,
      .dst_addr = esp_zb_get_short_address(),
  };
  esp_zb_zdo_mgmt_lqi_req(&req, lqi_rsp_cb, NULL);
  esp_zb_scheduler_alarm(lqi_alarm_cb, 0, ZB_LQI_POLL_MS);
}

static void kick_lqi_poll(void)
{
  if (s_lqi_polling)
    return;
  s_lqi_polling = true;
  esp_zb_scheduler_alarm(lqi_alarm_cb, 0, ZB_LQI_FIRST_MS);
}

// ---- cross-task entry: scheduler alarms run these in ZB context ----

static void steer_alarm_cb(uint8_t param)
{
  (void)param;
  if (!zigbee_pairing_open())
    return;
  ESP_LOGI(TAG, "Starting steering");
  esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
}

// DEVICE_REBOOT failure retry: re-run INITIALIZATION so the stack re-emits
// FIRST_START/REBOOT. Attempt count resets on a successful rejoin. Retries
// forever (fast then 60s cadence): a marginal link or a down coordinator
// must never strand the device until a manual reboot.
static uint32_t s_reboot_attempts = 0;

static void reboot_alarm_cb(uint8_t param)
{
  (void)param;
  ESP_LOGI(TAG, "Retrying initialization (rejoin attempt %lu)", (unsigned long)(s_reboot_attempts + 1));
  esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
}

static void schedule_rejoin_retry(void)
{
  int32_t delay = zigbee_rejoin_delay_ms(s_reboot_attempts);
  s_reboot_attempts++;
  ESP_LOGI(TAG, "Rejoin retry %lu in %ldms", (unsigned long)s_reboot_attempts, (long)delay);
  esp_zb_scheduler_alarm(reboot_alarm_cb, 0, (uint32_t)delay);
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

static const char *zb_sig_name(esp_zb_app_signal_type_t sig)
{
  switch (sig)
  {
  case ESP_ZB_ZDO_SIGNAL_DEFAULT_START:
    return "DEFAULT_START";
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    return "SKIP_STARTUP";
  case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
    return "DEVICE_ANNCE";
  case ESP_ZB_ZDO_SIGNAL_LEAVE:
    return "LEAVE";
  case ESP_ZB_ZDO_SIGNAL_ERROR:
    return "ERROR";
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    return "DEVICE_FIRST_START";
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    return "DEVICE_REBOOT";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK_NWK_STARTED:
    return "TOUCHLINK_NWK_STARTED";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK_NWK_JOINED_ROUTER:
    return "TOUCHLINK_NWK_JOINED_ROUTER";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK:
    return "TOUCHLINK";
  case ESP_ZB_BDB_SIGNAL_STEERING:
    return "STEERING";
  case ESP_ZB_BDB_SIGNAL_FORMATION:
    return "FORMATION";
  case ESP_ZB_BDB_SIGNAL_FINDING_AND_BINDING_TARGET_FINISHED:
    return "FIND_BIND_TARGET_FINISHED";
  case ESP_ZB_BDB_SIGNAL_FINDING_AND_BINDING_INITIATOR_FINISHED:
    return "FIND_BIND_INITIATOR_FINISHED";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK_TARGET:
    return "TOUCHLINK_TARGET";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK_NWK:
    return "TOUCHLINK_NWK";
  case ESP_ZB_BDB_SIGNAL_TOUCHLINK_TARGET_FINISHED:
    return "TOUCHLINK_TARGET_FINISHED";
  case ESP_ZB_NWK_SIGNAL_DEVICE_ASSOCIATED:
    return "DEVICE_ASSOCIATED";
  case ESP_ZB_ZDO_SIGNAL_LEAVE_INDICATION:
    return "LEAVE_INDICATION";
  case ESP_ZB_ZGP_SIGNAL_COMMISSIONING:
    return "ZGP_COMMISSIONING";
  case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
    return "CAN_SLEEP";
  case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
    return "PRODUCTION_CONFIG_READY";
  case ESP_ZB_NWK_SIGNAL_NO_ACTIVE_LINKS_LEFT:
    return "NO_ACTIVE_LINKS_LEFT";
  case ESP_ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED:
    return "DEVICE_AUTHORIZED";
  case ESP_ZB_ZDO_SIGNAL_DEVICE_UPDATE:
    return "DEVICE_UPDATE";
  case ESP_ZB_NWK_SIGNAL_PANID_CONFLICT_DETECTED:
    return "PANID_CONFLICT";
  case ESP_ZB_NLME_STATUS_INDICATION:
    return "NLME_STATUS";
  case ESP_ZB_BDB_SIGNAL_TC_REJOIN_DONE:
    return "TC_REJOIN_DONE";
  case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
    return "PERMIT_JOIN_STATUS";
  case ESP_ZB_BDB_SIGNAL_STEERING_CANCELLED:
    return "STEERING_CANCELLED";
  case ESP_ZB_BDB_SIGNAL_FORMATION_CANCELLED:
    return "FORMATION_CANCELLED";
  case ESP_ZB_ZGP_SIGNAL_MODE_CHANGE:
    return "ZGP_MODE_CHANGE";
  case ESP_ZB_ZDO_DEVICE_UNAVAILABLE:
    return "DEVICE_UNAVAILABLE";
  case ESP_ZB_ZGP_SIGNAL_APPROVE_COMMISSIONING:
    return "ZGP_APPROVE_COMMISSIONING";
  case ESP_ZB_SIGNAL_END:
    return "SIGNAL_END";
  default:
    return "UNKNOWN";
  }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *s)
{
  esp_zb_app_signal_type_t sig = *s->p_app_signal;
  ESP_LOGI(TAG, "Signal %s(%d): err=%s(0x%x)", zb_sig_name(sig), (int)sig,
           esp_err_to_name(s->esp_err_status), s->esp_err_status);
  switch (sig)
  {
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    break;
  case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
    // ESP_OK = network/production config restored from zb_storage;
    // ESP_FAIL = nothing stored (fresh flash or wiped NVRAM) — benign on a
    // factory-new device, fatal to rejoin if we had previously joined.
    ESP_LOGI(TAG, "Production config: %s (factory_new=%d)",
             s->esp_err_status == ESP_OK ? "restored from zb_storage" : "none in storage",
             (int)esp_zb_bdb_is_factory_new());
    // Early commissioned flag so the UI shows "Reconnecting…" (not
    // "Not joined") while the rejoin handshake runs.
    zigbee_on_commissioned(s->esp_err_status == ESP_OK);
    break;
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    if (s->esp_err_status != ESP_OK)
    {
      // NOTE: on lib 1.x a failed rejoin arrives as a generic ZBOSS error
      // (0xffffffff), NOT a BDB sub-status — the stack doesn't say whether
      // no parent was heard or the trust center stayed silent. A NULL name
      // means exactly that; repeated hits with factory_new=0 are the
      // rejoin-not-completing loop (same story as the sample's NO_NETWORK).
      const char *bdb = zigbee_bdb_status_name((uint8_t)s->esp_err_status);
      if (bdb)
      {
        ESP_LOGW(TAG, "Stack init failed: sig=%s(%d) bdb=%s(0x%x) factory_new=%d ch=%d pan=0x%04x",
                 zb_sig_name(sig), (int)sig, bdb,
                 s->esp_err_status, (int)esp_zb_bdb_is_factory_new(),
                 esp_zb_get_current_channel(), esp_zb_get_pan_id());
      }
      else
      {
        ESP_LOGW(TAG, "Stack init failed: sig=%s(%d) status=0x%x factory_new=%d ch=%d pan=0x%04x (rejoin incomplete)",
                 zb_sig_name(sig), (int)sig,
                 s->esp_err_status, (int)esp_zb_bdb_is_factory_new(),
                 esp_zb_get_current_channel(), esp_zb_get_pan_id());
      }
      // Network data survived (factory_new=0) but the rejoin did not
      // complete: retry, the parent/coordinator may just not be ready yet.
      if (!esp_zb_bdb_is_factory_new())
      {
        zigbee_on_commissioned(true);
        schedule_rejoin_retry();
      }
      break;
    }
    if (esp_zb_bdb_is_factory_new())
    {
      ESP_LOGI(TAG, "Factory new (sig=%s)", zb_sig_name(sig));
      zigbee_on_commissioned(false);
      if (zigbee_pairing_open())
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
      else
        ESP_LOGI(TAG, "Factory new, pairing closed: idle until pairing window opens");
    }
    else
    {
      s_reboot_attempts = 0;
      ESP_LOGI(TAG, "Rejoined (ch %d, pan 0x%04x)", esp_zb_get_current_channel(), esp_zb_get_pan_id());
      zigbee_on_joined(esp_zb_get_current_channel(), esp_zb_get_pan_id());
      kick_lqi_poll();
    }
    break;
  case ESP_ZB_BDB_SIGNAL_STEERING:
    if (s->esp_err_status == ESP_OK)
    {
      ESP_LOGI(TAG, "Joined (ch %d, pan 0x%04x): keep powered ~10s so NVRAM commits to zb_storage",
               esp_zb_get_current_channel(), esp_zb_get_pan_id());
      zigbee_on_joined(esp_zb_get_current_channel(), esp_zb_get_pan_id());
      kick_lqi_poll();
    }
    else if (zigbee_pairing_open())
    {
      const char *bdb = zigbee_bdb_status_name((uint8_t)s->esp_err_status);
      if (bdb)
        ESP_LOGI(TAG, "Steering failed (bdb=%s 0x%x), retry", bdb, s->esp_err_status);
      else
        ESP_LOGI(TAG, "Steering failed (status=0x%x), retry", s->esp_err_status);
      esp_zb_scheduler_alarm(steer_alarm_cb, 0, 1000); // retry while window open
    }
    else
    {
      const char *bdb = zigbee_bdb_status_name((uint8_t)s->esp_err_status);
      if (bdb)
        ESP_LOGI(TAG, "Steering stopped (bdb=%s 0x%x)", bdb, s->esp_err_status);
      else
        ESP_LOGI(TAG, "Steering stopped (status=0x%x)", s->esp_err_status);
    }
    break;
  case ESP_ZB_ZDO_SIGNAL_LEAVE:
    ESP_LOGI(TAG, "Local leave confirmed");
    zigbee_on_left();
    break;
  case ESP_ZB_NWK_SIGNAL_DEVICE_ASSOCIATED:
  case ESP_ZB_BDB_SIGNAL_TC_REJOIN_DONE:
  case ESP_ZB_NWK_SIGNAL_PANID_CONFLICT_DETECTED:
  case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
  case ESP_ZB_NLME_STATUS_INDICATION:
    // Rejoin handshake progress: ASSOCIATED = a parent answered, TC_REJOIN_DONE
    // = trust center authorized (or not). Visible at INFO so a far device's
    // parent search can be followed in the logs.
    ESP_LOGI(TAG, "Net %s(%d): err=%s(0x%x)", zb_sig_name(sig), (int)sig,
             esp_err_to_name(s->esp_err_status), s->esp_err_status);
    break;
  default:
    ESP_LOGD(TAG, "Unhandled signal %d", (int)sig);
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
  ESP_LOGI(TAG, "Build endpoints: light=%d lock=%d", (int)caps.light, (int)caps.lock);
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
  // Let Wi-Fi association finish before 802.15.4 owns the radio: at boot
  // the STA connect burst and the ZB rejoin fight over the single 2.4 GHz
  // frontend and the rejoin loses (DEVICE_REBOOT ESP_FAIL). Skipped when no
  // STA credentials exist (pure AP mode: nothing to wait for) and bounded
  // otherwise so a down AP never holds Zigbee hostage.
  if (!is_wifi_setup())
  {
    ESP_LOGI(TAG, "Radio start: no STA creds (AP mode), skipping wifi wait");
  }
  else
  {
    uint32_t waited_ms = 0;
    while (!is_wifi_connected() && waited_ms < ZB_WIFI_WAIT_MAX_MS)
    {
      vTaskDelay(pdMS_TO_TICKS(ZB_WIFI_WAIT_POLL_MS));
      waited_ms += ZB_WIFI_WAIT_POLL_MS;
    }
    ESP_LOGI(TAG, "Radio start: wifi_connected=%d after %lums", (int)is_wifi_connected(),
             (unsigned long)waited_ms);
  }
  static bool platform_done = false;
  if (!platform_done)
  {
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    esp_err_t err = esp_zb_platform_config(&platform_cfg);
    ESP_LOGI(TAG, "platform_config: %s(0x%x)", esp_err_to_name(err), err);
    ESP_ERROR_CHECK(err);
    platform_done = true;
  }

  // Coex arbiter BEFORE esp_zb_start: once 802.15.4 RX owns the radio,
  // Wi-Fi loses receive windows and all traffic dies (beacon timeouts,
  // reason-200 drops, dead web UI). Wi-Fi is already up (wifi-first boot),
  // so pre-start is the earliest possible point.
  {
    esp_err_t err = esp_coex_wifi_i154_enable();
    ESP_LOGI(TAG, "coex_enable: %s(0x%x)", esp_err_to_name(err), err);
    ESP_ERROR_CHECK(err);
  }

  esp_zb_cfg_t zb_cfg = {
      .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
      .install_code_policy = false,
      .nwk_cfg.zed_cfg = {
          .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
          .keep_alive = 3000,
      },
  };
  esp_zb_init(&zb_cfg);
  ESP_LOGI(TAG, "zb_init done, factory_new=%d", (int)esp_zb_bdb_is_factory_new());
  esp_zb_set_tx_power(ZB_TX_POWER_DBM);
  {
    int8_t applied = 0;
    esp_zb_get_tx_power(&applied);
    ESP_LOGI(TAG, "tx_power: requested=%d applied=%d dBm", ZB_TX_POWER_DBM, applied);
  }
  esp_zb_bdb_set_scan_duration(ZB_SCAN_DURATION);
  ESP_LOGI(TAG, "scan_duration: %u", (unsigned)esp_zb_bdb_get_scan_duration());
  esp_zb_device_register(build_endpoints());
  ESP_LOGI(TAG, "device_register done");
  esp_zb_core_action_handler_register(zb_action_handler);
  {
    esp_err_t err = esp_zb_start(false);
    ESP_LOGI(TAG, "zb_start(false): %s(0x%x)", esp_err_to_name(err), err);
    ESP_ERROR_CHECK(err);
  }
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
