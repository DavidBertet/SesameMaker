// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "zb_transport.h"

#include "channel_config.h"
#include "wifi.h"
#include "zigbee_bdb.h"
#include "zigbee_lqi.h"
#include "zigbee_rejoin.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdlib.h>

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED

// Native ezb_ API throughout. The only cross-task primitive needed is
// esp_timer one-shots that take the ZB lock, same pattern as Espressif's
// own examples.
#include "esp_zigbee.h"
#include "ezbee/af.h"
#include "ezbee/zha.h"
#include "ezbee/zcl/cluster/basic_desc.h"
#include "ezbee/zdo/zdo_nwk_mgmt.h"
#include "esp_coexist.h"

// zigbee_lqi.h compares relationships numerically so host tests need no IDF
// headers; the firmware build pins the value to the real enum here.
_Static_assert(EZB_NWK_RELATIONSHIP_PARENT == ZB_LQI_PARENT_RELATIONSHIP,
               "parent relationship mismatch");

// Same deal for zigbee_bdb.h: BDB commissioning statuses, pinned here so
// a renumber breaks the build. The stack delivers these in the signal
// payload (that's where NO_NETWORK et al. come from).
_Static_assert(EZB_BDB_STATUS_SUCCESS == 0, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_IN_PROGRESS == 1, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NOT_AA_CAPABLE == 2, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NO_NETWORK == 3, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_TARGET_FAILURE == 4, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_FORMATION_FAILURE == 5, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NO_IDENTIFY_QUERY_RESPONSE == 6, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_BINDING_TABLE_FULL == 7, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NO_SCAN_RESPONSE == 8, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NOT_PERMITTED == 9, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_TCLK_EX_FAILURE == 10, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_NOT_ON_A_NETWORK == 11, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_ON_A_NETWORK == 12, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_CANCELLED == 13, "bdb status mismatch");
_Static_assert(EZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE == 14, "bdb status mismatch");

static const char *TAG = "ZB_STACK";

#define ZB_TASK_STACK 8192
#define ZB_TASK_PRIO 5
// 802.15.4 TX power in dBm (C6 range -15..+20). Max: the garage is often far
// from the coordinator/routers and this is a mains-powered device.
#define ZB_TX_POWER_DBM 20
// Max time to let Wi-Fi associate before starting the 802.15.4 radio.
// Applies only when STA credentials exist; pure-AP boots skip the wait.
// Polls is_wifi_connected(); starts early on GOT_IP, starts anyway at the
// cap so a down AP never blocks Zigbee.
#define ZB_WIFI_WAIT_MAX_MS 15000
#define ZB_WIFI_WAIT_POLL_MS 500

// Length-prefixed ZCL strings live in the app (device identity); the
// length byte stays a separate literal: "\x0bD..." would parse as \xBD
// (D is a hex digit), corrupting the attr.

static bool s_started = false;
// Written by the ZB task, read by WS/button tasks: mark volatile so the
// compiler never caches the value across task boundaries.
static volatile bool s_ready = false; // esp_zigbee_start() done, timer callbacks safe

// Configured scan channel (0 = auto/all, 11..26 = pinned). Set by
// zigbee.c before the stack task runs; read inside zb_task, so no
// cross-task synchronization is needed after s_started.
static uint8_t s_scan_channel = 0;

// Bound app contract (copied at register; string/table data is app-static).
static zb_app_t s_app;
static bool s_app_bound = false;

// Pairing gate: unbound or missing callback means closed (safe default).
static bool app_pairing_open(void)
{
  return s_app_bound && s_app.pairing_open && s_app.pairing_open();
}

// Last latched value per endpoint slot (dedupe; valid=false = never).
// Slots follow registration order; reports for unknown endpoints are dropped.
static struct
{
  bool valid;
  uint8_t ep;
  uint8_t val;
} s_last[ZB_TRANSPORT_MAX_ENDPOINTS];

// Pending attribute snapshot, applied in ZB context by report_alarm_cb.
static struct
{
  bool has;
  uint8_t ep;
  uint8_t val;
} s_pending[ZB_TRANSPORT_MAX_ENDPOINTS];

// ---- deferred ZB work (esp_timer one-shot + ZB lock) ----
//
// Any task (WS handlers, button, rejoin logic) schedules work, the timer
// fires it with the stack lock held, same as Espressif's alarm_timer
// example util.

typedef struct
{
  esp_timer_handle_t timer;
  void (*cb)(uint32_t);
  uint32_t arg;
} zb_alarm_t;

static void zb_alarm_fire(void *a)
{
  zb_alarm_t *al = (zb_alarm_t *)a;
  esp_timer_delete(al->timer);
  void (*cb)(uint32_t) = al->cb;
  uint32_t arg = al->arg;
  free(al);
  if (!s_ready)
    return;
  if (!esp_zigbee_lock_acquire(portMAX_DELAY))
    return;
  cb(arg);
  esp_zigbee_lock_release();
}

static void zb_alarm_in(void (*cb)(uint32_t), uint32_t arg, uint32_t ms)
{
  zb_alarm_t *al = (zb_alarm_t *)calloc(1, sizeof(*al));
  if (!al)
    return;
  al->cb = cb;
  al->arg = arg;
  esp_timer_create_args_t t = {
      .callback = zb_alarm_fire,
      .arg = al,
      .name = "zb",
  };
  if (esp_timer_create(&t, &al->timer) != ESP_OK ||
      esp_timer_start_once(al->timer, (uint64_t)ms * 1000) != ESP_OK)
  {
    if (al->timer)
      esp_timer_delete(al->timer);
    free(al);
  }
}

// ---- parent-link LQI poll (Mgmt_Lqi_req to self) ----

// Set by any task in kick_lqi_poll, cleared by the ZB context callback;
// volatile keeps the check-and-set from being reordered/cached across tasks.
static volatile bool s_lqi_polling = false;
// When the current poll was kicked (esp_timer_get_time). Lets kick_lqi_poll
// self-heal if the req send fails silently or the rsp never arrives.
static volatile int64_t s_lqi_kick_us = 0;
#define ZB_LQI_STUCK_US 15000000

static void lqi_rsp_cb(const ezb_zdo_nwk_mgmt_lqi_req_result_t *result, void *ctx)
{
  (void)ctx;
  s_lqi_polling = false;
  if (!result || result->error != EZB_ERR_NONE || !result->rsp ||
      result->rsp->status != 0 || !result->rsp->neighbor_table_list)
  {
    ESP_LOGD(TAG, "LQI rsp missing");
    return;
  }
  uint8_t total = result->rsp->neighbor_table_list_count;
  uint8_t count = total > ZB_LQI_MAX_ENTRIES ? ZB_LQI_MAX_ENTRIES : total;
  if (total > ZB_LQI_MAX_ENTRIES)
  {
    ESP_LOGI(TAG, "LQI rsp truncated: %u entries, scanning first %u", total, ZB_LQI_MAX_ENTRIES);
  }
  // Host-tested parent search over the relationship bytes.
  uint8_t relationships[ZB_LQI_MAX_ENTRIES];
  for (uint8_t i = 0; i < count; i++)
  {
    const ezb_zdp_nwk_mgmt_lqi_neighbor_table_entry_t *e = &result->rsp->neighbor_table_list[i];
    relationships[i] = e->affinity;
    ESP_LOGD(TAG, "LQI entry %u: addr 0x%04x affinity %u depth %u lqa %u", i, e->nwk_addr,
             e->affinity, e->device_depth, e->lqa);
  }
  int idx = zigbee_parent_index(relationships, count);
  if (idx < 0)
  {
    ESP_LOGI(TAG, "LQI poll: no parent entry in %u neighbors", total);
    return;
  }
  const ezb_zdp_nwk_mgmt_lqi_neighbor_table_entry_t *n = &result->rsp->neighbor_table_list[idx];
  ESP_LOGI(TAG, "LQI parent 0x%04x depth %u lqa=%d", n->nwk_addr, n->device_depth, n->lqa);
  if (s_app_bound && s_app.on_parent)
    s_app.on_parent(n->nwk_addr, n->device_depth, n->lqa);
}

static void lqi_alarm_cb(uint32_t arg)
{
  (void)arg;
  if (!s_ready || !ezb_bdb_dev_joined())
  {
    s_lqi_polling = false;
    return;
  }
  ezb_zdo_nwk_mgmt_lqi_req_t req = {
      .dst_nwk_addr = ezb_nwk_get_short_address(),
      .field = {.start_index = 0},
      .cb = lqi_rsp_cb,
      .user_ctx = NULL,
  };
  if (ezb_zdo_nwk_mgmt_lqi_req(&req) != EZB_ERR_NONE)
  {
    s_lqi_polling = false;
  }
  // One-shot: the next poll comes from kick_lqi_poll or
  // zb_transport_poll_lqi, never from here.
}

static void kick_lqi_poll(void)
{
  if (s_lqi_polling)
  {
    // Self-heal: req send failed silently or rsp never arrived.
    if (esp_timer_get_time() - s_lqi_kick_us < ZB_LQI_STUCK_US)
      return;
  }
  s_lqi_polling = true;
  s_lqi_kick_us = esp_timer_get_time();
  zb_alarm_in(lqi_alarm_cb, 0, ZB_LQI_FIRST_MS);
}

// Request a fresh parent-link reading (e.g. UI opened the card). The
// reply is async and lands via broadcast_zigbee_config; safe from any
// task, no-op while unjoined or when one is already in flight.
void zb_transport_poll_lqi(void)
{
  if (!s_ready)
    return;
  kick_lqi_poll();
}

// ---- cross-task entries (run with the ZB lock held) ----

static void steer_alarm_cb(uint32_t mode)
{
  if (!app_pairing_open())
    return;
  ESP_LOGI(TAG, "Starting steering");
  ezb_bdb_start_top_level_commissioning((ezb_bdb_comm_mode_mask_t)mode);
}

// DEVICE_REBOOT failure retry: re-run INITIALIZATION so the stack re-emits
// FIRST_START/REBOOT. Attempt count resets on a successful rejoin. Retries
// forever (5s x20 then 30s cadence, see zigbee_rejoin.h).
static uint32_t s_reboot_attempts = 0;

static void reboot_alarm_cb(uint32_t arg)
{
  (void)arg;
  ESP_LOGI(TAG, "Retrying initialization (rejoin attempt %lu)", (unsigned long)(s_reboot_attempts + 1));
  ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
}

static void schedule_rejoin_retry(void)
{
  int32_t delay = zigbee_rejoin_delay_ms(s_reboot_attempts);
  s_reboot_attempts++;
  ESP_LOGI(TAG, "Rejoin retry %lu in %ldms", (unsigned long)s_reboot_attempts, (long)delay);
  zb_alarm_in(reboot_alarm_cb, 0, (uint32_t)delay);
}

static void leave_alarm_cb(uint32_t arg)
{
  (void)arg;
  ESP_LOGI(TAG, "Local leave");
  ezb_bdb_reset_via_local_action();
}

static void reset_alarm_cb(uint32_t arg)
{
  (void)arg;
  ESP_LOGI(TAG, "Factory reset");
  esp_zigbee_factory_reset();
}

// ---- pending attribute snapshot, applied in ZB context by report_alarm_cb ----

static void report_alarm_cb(uint32_t arg)
{
  (void)arg;
  for (size_t i = 0; i < ZB_TRANSPORT_MAX_ENDPOINTS; i++)
  {
    if (!s_pending[i].has)
      continue;
    ezb_zcl_set_attr_value(s_pending[i].ep, EZB_ZCL_CLUSTER_ID_ON_OFF,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                           EZB_ZCL_STD_MANUF_CODE, &s_pending[i].val, false);
    s_pending[i].has = false;
  }
}

// ---- inbound commands ----

static void handle_onoff(const ezb_zcl_set_attr_value_message_t *m)
{
  if (m->info.cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF ||
      m->in.attribute.id != EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID ||
      m->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_BOOL || !m->in.attribute.data.value)
    return;
  if (!s_app_bound || !s_app.on_onoff_cmd)
    return;
  s_app.on_onoff_cmd(m->info.dst_ep, *(bool *)m->in.attribute.data.value);
}

static void zb_action_handler(ezb_zcl_core_action_callback_id_t id, void *msg)
{
  switch (id)
  {
  case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID:
    handle_onoff((const ezb_zcl_set_attr_value_message_t *)msg);
    break;
  default:
    break;
  }
}

// ---- commissioning signals (run in ZB context, no lock needed) ----

static bool ezb_signal_handler(const ezb_app_signal_t *s)
{
  ezb_app_signal_type_t sig = ezb_app_signal_get_type(s);
  ESP_LOGI(TAG, "Signal %s", ezb_app_signal_to_string(sig));
  switch (sig)
  {
  case EZB_ZDO_SIGNAL_SKIP_STARTUP:
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
    break;
  case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case EZB_BDB_SIGNAL_DEVICE_REBOOT:
  {
    ezb_bdb_comm_status_t st = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(s);
    if (st != EZB_BDB_STATUS_SUCCESS)
    {
      // The signal payload carries the BDB sub-status (NO_NETWORK = scan
      // heard no usable parent, TCLK_EX_FAILURE = trust-center key
      // exchange failed).
      const char *bdb = zigbee_bdb_status_name(st);
      if (bdb)
      {
        ESP_LOGW(TAG, "Stack init failed: sig=%s bdb=%s(0x%x) factory_new=%d ch=%d pan=0x%04x",
                 ezb_app_signal_to_string(sig), bdb, st,
                 (int)ezb_bdb_is_factory_new(),
                 ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      }
      else
      {
        ESP_LOGW(TAG, "Stack init failed: sig=%s status=0x%x factory_new=%d ch=%d pan=0x%04x (rejoin incomplete)",
                 ezb_app_signal_to_string(sig), st,
                 (int)ezb_bdb_is_factory_new(),
                 ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      }
      // Network data survived (factory_new=0) but the rejoin did not
      // complete: retry, the parent/coordinator may just not be ready yet.
      if (!ezb_bdb_is_factory_new())
      {
        if (s_app_bound && s_app.on_commissioned)
          s_app.on_commissioned(true);
        schedule_rejoin_retry();
      }
      break;
    }
    if (ezb_bdb_is_factory_new())
    {
      ESP_LOGI(TAG, "Factory new (sig=%s)", ezb_app_signal_to_string(sig));
      if (s_app_bound && s_app.on_commissioned)
        s_app.on_commissioned(false);
      if (app_pairing_open())
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
      else
        ESP_LOGI(TAG, "Factory new, pairing closed: idle until pairing window opens");
    }
    else
    {
      s_reboot_attempts = 0;
      ESP_LOGI(TAG, "Rejoined (ch %d, pan 0x%04x)", ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      if (s_app_bound && s_app.on_joined)
        s_app.on_joined(ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      kick_lqi_poll();
    }
    break;
  }
  case EZB_BDB_SIGNAL_STEERING:
  {
    ezb_bdb_comm_status_t st = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(s);
    if (st == EZB_BDB_STATUS_SUCCESS)
    {
      ESP_LOGI(TAG, "Joined (ch %d, pan 0x%04x): keep powered ~10s so NVRAM commits",
               ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      if (s_app_bound && s_app.on_joined)
        s_app.on_joined(ezb_nwk_get_current_channel(), ezb_nwk_get_panid());
      kick_lqi_poll();
    }
    else if (app_pairing_open())
    {
      const char *bdb = zigbee_bdb_status_name(st);
      if (bdb)
        ESP_LOGI(TAG, "Steering failed (bdb=%s 0x%x), retry", bdb, st);
      else
        ESP_LOGI(TAG, "Steering failed (status=0x%x), retry", st);
      zb_alarm_in(steer_alarm_cb, EZB_BDB_MODE_NETWORK_STEERING, 500); // retry while window open
    }
    else
    {
      const char *bdb = zigbee_bdb_status_name(st);
      if (bdb)
        ESP_LOGI(TAG, "Steering stopped (bdb=%s 0x%x)", bdb, st);
      else
        ESP_LOGI(TAG, "Steering stopped (status=0x%x)", st);
    }
    break;
  }
  case EZB_ZDO_SIGNAL_LEAVE:
  {
    const ezb_zdo_signal_leave_params_t *lp = ezb_app_signal_get_params(s);
    ESP_LOGI(TAG, "Local leave confirmed (type=0x%02x)", lp ? lp->leave_type : 0);
    if (s_app_bound && s_app.on_left)
      s_app.on_left();
    break;
  }
  case EZB_NWK_SIGNAL_DEVICE_ASSOCIATED:
  case EZB_NWK_SIGNAL_PANID_CONFLICT_DETECTED:
  case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
    // Rejoin handshake progress: ASSOCIATED = a parent answered.
    // Visible at INFO so a far device's parent search can be followed
    // in the logs.
    ESP_LOGI(TAG, "Net %s", ezb_app_signal_to_string(sig));
    break;
  default:
    ESP_LOGD(TAG, "Unhandled signal %s", ezb_app_signal_to_string(sig));
    break;
  }
  return true;
}

// ---- endpoints ----

// Stock On/Off Light template (Basic + Identify + Groups + Scenes +
// On/Off servers), stamped with the app's manufacturer/model, device ID
// overridden per entry. Deliberately plain switches only: no lock-cluster
// PIN/keypad baggage, no Window Covering sliders for binary functions.
static ezb_af_ep_desc_t output_endpoint(uint8_t endpoint, uint16_t device_id)
{
  ezb_zha_on_off_light_config_t light_cfg = EZB_ZHA_ON_OFF_LIGHT_CONFIG();
  ezb_af_ep_desc_t ep = ezb_zha_create_on_off_light(endpoint, &light_cfg);
  ezb_zcl_cluster_desc_t basic =
      ezb_af_endpoint_get_cluster_desc(ep, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
  ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                      (void *)s_app.manufacturer_name);
  ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                      (void *)s_app.model_identifier);
  if (device_id != EZB_ZHA_ON_OFF_LIGHT_DEVICE_ID)
    ezb_af_ep_desc_set_app_device_id(ep, device_id);
  return ep;
}

static void build_endpoints(void)
{
  zb_endpoint_desc_t table[ZB_TRANSPORT_MAX_ENDPOINTS];
  size_t n = 0;
  if (s_app_bound && s_app.build_table)
    n = s_app.build_table(table, ZB_TRANSPORT_MAX_ENDPOINTS);
  if (n > ZB_TRANSPORT_MAX_ENDPOINTS)
    n = ZB_TRANSPORT_MAX_ENDPOINTS;
  ESP_LOGI(TAG, "Build endpoints: %u", (unsigned)n);
  ezb_af_device_desc_t dev = ezb_af_create_device_desc();
  for (size_t i = 0; i < n; i++)
  {
    ezb_af_device_add_endpoint_desc(
        dev, output_endpoint(table[i].ep, table[i].device_id));
  }
  ESP_ERROR_CHECK(ezb_af_device_desc_register(dev));
  ESP_LOGI(TAG, "device_register done");
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

  // Coex arbiter BEFORE esp_zigbee_start: once 802.15.4 RX owns the radio,
  // Wi-Fi loses receive windows and all traffic dies (beacon timeouts,
  // reason-200 drops, dead web UI). Wi-Fi is already up (wifi-first boot),
  // so pre-start is the earliest possible point.
  {
    esp_err_t err = esp_coex_wifi_i154_enable();
    ESP_LOGI(TAG, "coex_enable: %s(0x%x)", esp_err_to_name(err), err);
    ESP_ERROR_CHECK(err);
  }

  esp_zigbee_config_t zb_cfg = {
      .device_config = {
          .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
          .install_code_policy = false,
          .zed_config = {
              .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,
              .keep_alive = 3000,
          },
      },
      .platform_config = {
          // ZBOSS persistence lives in its own dedicated flash partition
          // (zb_storage, 16 KB) so the network/security dataset never shares
          // NVS pages with Wi-Fi or the app blob. A full shared NVS would
          // wedge both radios' storage together and can fail to commit on
          // every factory reset.
          .storage_partition_name = "zb_storage",
          .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
      },
  };
  ESP_ERROR_CHECK(esp_zigbee_init(&zb_cfg));
  ESP_LOGI(TAG, "zb_init done, factory_new=%d", (int)ezb_bdb_is_factory_new());
  // Early commissioned flag so the UI shows "Reconnecting…" (not
  // "Not joined") while the rejoin handshake runs; the FIRST_START /
  // REBOOT signals correct it moments later if wrong.
  if (s_app_bound && s_app.on_commissioned)
    s_app.on_commissioned(!ezb_bdb_is_factory_new());
  ezb_set_tx_power(ZB_TX_POWER_DBM);
  {
    int8_t applied = 0;
    ezb_get_tx_power(&applied);
    ESP_LOGI(TAG, "tx_power: requested=%d applied=%d dBm", ZB_TX_POWER_DBM, applied);
  }
  ezb_bdb_set_scan_duration(zigbee_channel_scan_duration(s_scan_channel));
  ESP_LOGI(TAG, "scan_duration: %u", (unsigned)ezb_bdb_get_scan_duration());
  // Restrict BDB scanning to the configured channel when one is pinned:
  // full 16-channel scan on a marginal link misses the beacon response
  // window. Secondary must match: steering energy-scans BOTH masks, so
  // leaving the secondary at its default turns every attempt into a ~13s
  // all-channel energy scan before the parent search even starts.
  ezb_bdb_set_primary_channel_set(zigbee_channel_mask(s_scan_channel));
  ezb_bdb_set_secondary_channel_set(zigbee_channel_mask(s_scan_channel));
  ESP_LOGI(TAG, "bdb_ch_mask: 0x%04x", (unsigned)ezb_bdb_get_primary_channel_set());
  build_endpoints();
  ezb_zcl_core_action_handler_register(zb_action_handler);
  ezb_app_signal_add_handler(ezb_signal_handler);
  ezb_nwk_set_rx_on_when_idle(false);
  {
    esp_err_t err = esp_zigbee_start(false);
    ESP_LOGI(TAG, "zb_start(false): %s(0x%x)", esp_err_to_name(err), err);
    ESP_ERROR_CHECK(err);
  }
  s_ready = true;

  esp_zigbee_launch_mainloop();
  vTaskDelete(NULL);
}

// ---- public API ----

void zb_transport_register(const zb_app_t *app)
{
  if (!app)
    return;
  s_app = *app;
  s_app_bound = true;
}

void zb_transport_start(void)
{
  if (s_started)
    return;
  s_started = true;
  xTaskCreate(zb_task, "zb_main", ZB_TASK_STACK, NULL, ZB_TASK_PRIO, NULL);
}

// Re-apply the BDB scan channel mask + duration from ZB context (BDB reads
// them at scan time, so a change mid-steering takes effect on the next
// attempt; they are not read at esp_zigbee_start).
static void channel_alarm_cb(uint32_t ch)
{
  ezb_bdb_set_scan_duration(zigbee_channel_scan_duration((int)ch));
  ezb_bdb_set_primary_channel_set(zigbee_channel_mask((int)ch));
  ezb_bdb_set_secondary_channel_set(zigbee_channel_mask((int)ch));
  ESP_LOGI(TAG, "bdb_ch_mask: 0x%04x", (unsigned)ezb_bdb_get_primary_channel_set());
}

// Pin Zigbee scanning to a single channel (0 = auto/all). Safe from any
// task: applied in ZB context via alarm when the stack is already running.
void zb_transport_set_channel(uint8_t channel)
{
  if (channel != 0 && (channel < ZB_CHANNEL_CFG_MIN || channel > ZB_CHANNEL_CFG_MAX))
  {
    ESP_LOGW(TAG, "Ignoring invalid scan channel %u", (unsigned)channel);
    return;
  }
  s_scan_channel = channel;
  if (s_ready)
    zb_alarm_in(channel_alarm_cb, (uint32_t)channel, 100);
}

void zb_transport_pair(void)
{
  if (!s_ready)
  {
    ESP_LOGW(TAG, "Stack not ready, steering skipped");
    return;
  }
  zb_alarm_in(steer_alarm_cb, EZB_BDB_MODE_NETWORK_STEERING, 100);
}

void zb_transport_leave(void)
{
  if (!s_ready)
    return;
  zb_alarm_in(leave_alarm_cb, 0, 100);
}

void zb_transport_factory_reset(void)
{
  if (!s_ready)
    return;
  zb_alarm_in(reset_alarm_cb, 0, 100);
}

// Find the slot tracking ep, or -1. Slots are assigned in registration
// order on first report; reports for unregistered endpoints are dropped
// (the app only reports what its table built).
static int report_slot(uint8_t ep)
{
  for (size_t i = 0; i < ZB_TRANSPORT_MAX_ENDPOINTS; i++)
  {
    if (s_last[i].valid && s_last[i].ep == ep)
      return (int)i;
  }
  for (size_t i = 0; i < ZB_TRANSPORT_MAX_ENDPOINTS; i++)
  {
    if (!s_last[i].valid)
    {
      s_last[i].valid = true;
      s_last[i].ep = ep;
      s_last[i].val = 0xff;
      return (int)i;
    }
  }
  return -1;
}

void zb_transport_report_onoff(uint8_t ep, bool on)
{
  if (!s_ready)
    return;
  int slot = report_slot(ep);
  if (slot < 0)
  {
    ESP_LOGW(TAG, "Report for unknown ep %u dropped", (unsigned)ep);
    return;
  }
  uint8_t val = on ? 1 : 0;
  if (s_last[slot].val == val)
    return;
  s_last[slot].val = val;
  s_pending[slot].has = true;
  s_pending[slot].ep = ep;
  s_pending[slot].val = val;
  zb_alarm_in(report_alarm_cb, 0, 100);
}

#else // !CONFIG_SOC_IEEE802154_SUPPORTED

void zb_transport_register(const zb_app_t *app)
{
  (void)app;
}
void zb_transport_start(void) {}
void zb_transport_set_channel(uint8_t channel) { (void)channel; }
void zb_transport_pair(void) {}
void zb_transport_leave(void) {}
void zb_transport_factory_reset(void) {}
void zb_transport_report_onoff(uint8_t ep, bool on)
{
  (void)ep;
  (void)on;
}
void zb_transport_poll_lqi(void) {}

#endif
