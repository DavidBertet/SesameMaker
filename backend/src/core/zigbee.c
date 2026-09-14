// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Zigbee bridge (802.15.4 targets only, gated by IDF's
// CONFIG_SOC_IEEE802154_SUPPORTED).

#include "zigbee.h"

#include "channel_config.h"
#include "constants.h"
#include "storage.h"
#include "ws_zigbee.h"
#include "zb_transport.h"

#include "esp_log.h"

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include <string.h>

static const char *TAG = "ZIGBEE";

#define ZIG_CFG_KEY "zig_cfg"
#define ZIG_PAIR_MAX_S 180

typedef struct
{
  uint8_t enabled;
  uint8_t channel; // 0 = auto (all channels), 11..26 = pinned scan channel
  uint8_t _reserved[2];
} zig_persist_t;

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED

static bool s_enabled = false;
static uint8_t s_channel_cfg = 0; // 0 = auto/all, 11..26 = pinned scan channel
static bool s_joined = false;
static bool s_commissioned = false;
static uint16_t s_channel = 0;
static uint16_t s_pan_id = 0;
static uint8_t s_lqi = 0;
static bool s_lqi_valid = false;
static uint16_t s_parent_addr = 0;
static uint8_t s_parent_depth = 0;
static int64_t s_pair_until_us = 0;

// Bound to the transport in zigbee_init (defined below, after the
// callbacks it references).
static void zigbee_bind_transport(void);

static void persist(void)
{
  zig_persist_t p;
  memset(&p, 0, sizeof(p));
  p.enabled = s_enabled ? 1 : 0;
  p.channel = s_channel_cfg;
  write_blob(ZIG_CFG_KEY, &p, sizeof(p));
}

static void load(void)
{
  size_t n = 0;
  zig_persist_t p;
  memset(&p, 0, sizeof(p));
  if (read_blob(ZIG_CFG_KEY, NULL, &n) == ESP_OK && n == sizeof(p) &&
      read_blob(ZIG_CFG_KEY, &p, &n) == ESP_OK)
  {
    s_enabled = p.enabled != 0;
    s_channel_cfg = zigbee_channel_cfg_valid(p.channel) ? p.channel : 0;
  }
  else
  {
    persist();
  }
}

esp_err_t zigbee_init(void)
{
  load();
  zigbee_bind_transport();
  if (!s_enabled)
  {
    ESP_LOGI(TAG, "Zigbee disabled");
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Zigbee enabled, starting stack");
  zb_transport_set_channel(s_channel_cfg);
  zb_transport_start();
  return ESP_OK;
}

// True while a pairing window is open (steering target).
static bool zigbee_pairing_open(void)
{
  return s_pair_until_us > esp_timer_get_time();
}

// Transport callbacks (run in ZB task context): network joined/left.
static void zigbee_on_joined(uint16_t channel, uint16_t pan_id)
{
  s_joined = true;
  s_commissioned = true;
  s_channel = channel;
  s_pan_id = pan_id;
  s_pair_until_us = 0; // joined: window served its purpose
  broadcast_zigbee_config();
}

static void zigbee_on_commissioned(bool commissioned)
{
  if (s_commissioned == commissioned)
    return;
  s_commissioned = commissioned;
  broadcast_zigbee_config();
}

static void zigbee_on_left(void)
{
  s_joined = false;
  s_commissioned = false;
  s_channel = 0;
  s_pan_id = 0;
  s_lqi = 0;
  s_lqi_valid = false;
  s_parent_addr = 0;
  s_parent_depth = 0;
  broadcast_zigbee_config();
}

// Parent link from the stack's neighbor-table poll (addr, depth, LQI).
// Broadcasts only on change so the ~1/min poll doesn't spam WS clients
// with identical state.
static void zigbee_on_parent(uint16_t addr, uint8_t depth, uint8_t lqi)
{
  if (s_lqi_valid && s_lqi == lqi && s_parent_addr == addr && s_parent_depth == depth)
    return;
  s_parent_addr = addr;
  s_parent_depth = depth;
  s_lqi = lqi;
  s_lqi_valid = true;
  broadcast_zigbee_config();
}

// ---- Product content (registered zb_device_t) ----
//
// Endpoint table, command mapping, state reporting and device identity
// come from the app (e.g. zigbee_garage). This service only owns the
// generic end-device state machine; a second product registers its own
// content and reuses this file untouched. Register before zigbee_init().

static zb_device_t s_device;
static bool s_device_bound = false;

void zigbee_register_device(const zb_device_t *dev)
{
  if (!dev)
    return;
  s_device = *dev;
  s_device_bound = true;
}

// zb_app_t forwarders: transport-facing side stays here, product-facing
// side delegates to the registered device.
static size_t zb_build_table(zb_endpoint_desc_t *out, size_t max)
{
  if (s_device_bound && s_device.build_table)
    return s_device.build_table(out, max);
  return 0;
}

static void zb_on_attr_write(uint8_t ep, uint16_t cluster_id, uint16_t attr_id,
                             zb_attr_type_t type, const void *value)
{
  if (s_device_bound && s_device.on_attr_write)
    s_device.on_attr_write(ep, cluster_id, attr_id, type, value);
  else
    ESP_LOGW(TAG, "Attr write ep %u with no device bound", (unsigned)ep);
}

static const char *zb_manufacturer_name(void)
{
  if (s_device_bound && s_device.manufacturer_name)
    return s_device.manufacturer_name;
  return "";
}

static const char *zb_model_identifier(void)
{
  if (s_device_bound && s_device.model_identifier)
    return s_device.model_identifier;
  return "";
}

static void zigbee_bind_transport(void)
{
  zb_app_t app = {
      .build_table = zb_build_table,
      .manufacturer_name = zb_manufacturer_name(),
      .model_identifier = zb_model_identifier(),
      .on_attr_write = zb_on_attr_write,
      .on_joined = zigbee_on_joined,
      .on_left = zigbee_on_left,
      .on_commissioned = zigbee_on_commissioned,
      .on_parent = zigbee_on_parent,
      .pairing_open = zigbee_pairing_open,
  };
  zb_transport_register(&app);
}

esp_err_t zigbee_get_state(zigbee_state_t *out)
{
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->enabled = s_enabled;
  out->joined = s_joined;
  out->commissioned = s_commissioned;
  out->channel = s_channel;
  out->pan_id = s_pan_id;
  out->channel_cfg = s_channel_cfg;
  out->lqi = s_lqi;
  out->lqi_valid = s_lqi_valid;
  out->parent_addr = s_parent_addr;
  out->parent_depth = s_parent_depth;
  int64_t now = esp_timer_get_time();
  out->pairing_remaining_s =
      (s_pair_until_us > now) ? (uint32_t)((s_pair_until_us - now) / 1000000) : 0;
  return ESP_OK;
}

esp_err_t zigbee_apply_config(bool has_enabled, bool enabled,
                                bool has_channel, uint8_t channel)
{
  // Validate first: nothing is touched when the channel is invalid.
  if (has_channel && !zigbee_channel_cfg_valid(channel))
  {
    ESP_LOGW(TAG, "Invalid scan channel %u", (unsigned)channel);
    return ESP_ERR_INVALID_ARG;
  }
  bool enabled_changed = has_enabled && (enabled != s_enabled);
  bool channel_changed = has_channel && (channel != s_channel_cfg);
  if (has_enabled)
  {
    s_enabled = enabled;
    if (!enabled)
    {
      s_pair_until_us = 0;
      if (s_joined)
        zb_transport_leave();
    }
    else
    {
      zb_transport_start();
    }
  }
  if (has_channel)
  {
    s_channel_cfg = channel;
    zb_transport_set_channel(channel);
  }
  if (!enabled_changed && !channel_changed)
    return ESP_OK;
  persist();
  if (enabled_changed)
    ESP_LOGI(TAG, "Zigbee %s", s_enabled ? "enabled" : "disabled");
  if (channel_changed)
    ESP_LOGI(TAG, "Zigbee scan channel: %s",
             s_channel_cfg == 0 ? "auto (all channels)" : "pinned");
  broadcast_zigbee_config();
  return ESP_OK;
}

esp_err_t zigbee_set_enabled(bool enabled)
{
  return zigbee_apply_config(true, enabled, false, 0);
}

esp_err_t zigbee_set_channel(uint8_t channel)
{
  return zigbee_apply_config(false, false, true, channel);
}

esp_err_t zigbee_start_pairing(uint32_t duration_s)
{
  if (!s_enabled)
    return ESP_ERR_INVALID_STATE;
  // Already on a network: re-steering can't admit joiners (we're an ED,
  // not a router) and risks leaving the current network. Leave first.
  if (s_joined)
  {
    ESP_LOGW(TAG, "Pairing blocked: already joined, leave first");
    return ESP_ERR_INVALID_STATE;
  }
  if (duration_s == 0 || duration_s > ZIG_PAIR_MAX_S)
    duration_s = 60;
  s_pair_until_us = esp_timer_get_time() + (int64_t)duration_s * 1000000;
  zb_transport_pair();
  ESP_LOGI(TAG, "Pairing window open for %lus", (unsigned long)duration_s);
  broadcast_zigbee_config();
  return ESP_OK;
}

esp_err_t zigbee_leave(void)
{
  s_joined = false;
  s_channel = 0;
  s_pan_id = 0;
  s_pair_until_us = 0;
  zb_transport_leave();
  ESP_LOGI(TAG, "Left Zigbee network");
  zigbee_on_left(); // optimistic; signal handler confirms on rejoin paths
  return ESP_OK;
}

esp_err_t zigbee_factory_reset(void)
{
  s_joined = false;
  s_channel = 0;
  s_pan_id = 0;
  s_pair_until_us = 0;
  delete_blob(ZIG_CFG_KEY);
  s_enabled = false;
  zb_transport_factory_reset();
  ESP_LOGI(TAG, "Zigbee factory reset");
  zigbee_on_left();
  return ESP_OK;
}

void zigbee_report_state(void)
{
  if (!s_enabled || !s_joined)
    return;
  if (s_device_bound && s_device.report_state)
    s_device.report_state();
}

// ---- BOOT button: runtime sampling only, active-low ----

#define ZIG_BTN_POLL_MS 20
#define ZIG_BTN_DEBOUNCE_MS 50

static void zig_button_task(void *arg)
{
  (void)arg;
  if (ZIGBEE_BOOT_GPIO < 0)
    vTaskDelete(NULL);
  gpio_set_direction(ZIGBEE_BOOT_GPIO, GPIO_MODE_INPUT);
  // No pull config here: BOOT already has the devkit pullup; never drive it.
  uint32_t held_ms = 0;
  uint32_t low_ms = 0;
  bool acted_long = false;
  uint32_t last_pairing_s = 0;
  while (1)
  {
    // Countdown tick: push state each second while a pairing window runs,
    // plus once when it expires, so the webpage shows the live remaining
    // time no matter what opened the window (BOOT button or Pair button).
    int64_t now_us = esp_timer_get_time();
    uint32_t pairing_s =
        (s_pair_until_us > now_us) ? (uint32_t)((s_pair_until_us - now_us) / 1000000) : 0;
    if (pairing_s != last_pairing_s)
    {
      last_pairing_s = pairing_s;
      if (s_pair_until_us != 0)
      {
        if (pairing_s == 0)
          s_pair_until_us = 0; // expired: latch closed so we broadcast once
        broadcast_zigbee_config();
      }
    }
    bool low = gpio_get_level(ZIGBEE_BOOT_GPIO) == 0;
    if (low)
    {
      low_ms += ZIG_BTN_POLL_MS;
      if (low_ms >= ZIG_BTN_DEBOUNCE_MS)
        held_ms += ZIG_BTN_POLL_MS;
      if (!acted_long && held_ms >= ZIGBEE_PRESS_LONG_MS_MIN)
      {
        acted_long = true;
        ESP_LOGW(TAG, "BOOT long-press: factory reset");
        zigbee_factory_reset();
      }
    }
    else
    {
      if (low_ms >= ZIG_BTN_DEBOUNCE_MS && !acted_long)
      {
        if (zigbee_classify_press(held_ms) == ZIGBEE_PRESS_SHORT)
        {
          ESP_LOGI(TAG, "BOOT short-press: open pairing");
          if (s_enabled)
            zigbee_start_pairing(60);
          else
          {
            zigbee_set_enabled(true);
            zigbee_start_pairing(60);
          }
        }
      }
      low_ms = 0;
      held_ms = 0;
      acted_long = false;
    }
    vTaskDelay(pdMS_TO_TICKS(ZIG_BTN_POLL_MS));
  }
}

void zigbee_button_init(void)
{
  if (ZIGBEE_BOOT_GPIO < 0)
    return;
  // Doing boot button sampling & pairing-countdown broadcast path (format_state + WS send)
  xTaskCreate(zig_button_task, "zig_btn", 4096, NULL, 5, NULL);
}

#else // !CONFIG_SOC_IEEE802154_SUPPORTED

esp_err_t zigbee_init(void) { return ESP_OK; }

void zigbee_register_device(const zb_device_t *dev) { (void)dev; }

esp_err_t zigbee_get_state(zigbee_state_t *out)
{
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  return ESP_OK;
}

esp_err_t zigbee_set_enabled(bool enabled)
{
  (void)enabled;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_set_channel(uint8_t channel)
{
  (void)channel;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_apply_config(bool has_enabled, bool enabled,
                              bool has_channel, uint8_t channel)
{
  (void)has_enabled;
  (void)enabled;
  (void)has_channel;
  (void)channel;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_start_pairing(uint32_t duration_s)
{
  (void)duration_s;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_leave(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t zigbee_factory_reset(void) { return ESP_ERR_NOT_SUPPORTED; }
void zigbee_report_state(void) {}
void zigbee_button_init(void) {}

#endif // CONFIG_SOC_IEEE802154_SUPPORTED
