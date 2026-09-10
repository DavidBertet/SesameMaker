// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Zigbee bridge (802.15.4 targets only, gated by IDF's
// CONFIG_SOC_IEEE802154_SUPPORTED — no custom build flag).
//
// Current state: NVS-backed config + state machine + BOOT-button pairing.
// The real esp-zigbee-lib endpoint/cluster wiring (Window Covering 0x0102,
// On/Off 0x0006, Door Lock 0x0101, Binary Input, Occupancy) lands on top of
// this file: search for ZB_TODO. Without the IDF flag everything compiles to
// no-op stubs so the classic ESP32/C3 2MB build pays zero flash.

#include "zigbee.h"

#include "constants.h"
#include "garage_controller.h"
#include "storage.h"
#include "ws_zigbee.h"

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
  uint8_t _reserved[3];
} zig_persist_t;

#ifdef CONFIG_SOC_IEEE802154_SUPPORTED

static bool s_enabled = false;
static bool s_joined = false;
static uint16_t s_channel = 0;
static uint16_t s_pan_id = 0;
static int64_t s_pair_until_us = 0;

static void persist(void)
{
  zig_persist_t p;
  memset(&p, 0, sizeof(p));
  p.enabled = s_enabled ? 1 : 0;
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
  }
  else
  {
    persist();
  }
}

esp_err_t zigbee_init(void)
{
  load();
  if (!s_enabled)
  {
    ESP_LOGI(TAG, "Zigbee disabled");
    return ESP_OK;
  }
  // ZB_TODO: esp_zb_init + HA endpoint (cover/light/lock/sensors) +
  // BDB commissioning start. Until then: enabled flag only.
  ESP_LOGI(TAG, "Zigbee enabled (stack wiring pending)");
  return ESP_OK;
}

esp_err_t zigbee_get_state(zigbee_state_t *out)
{
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->enabled = s_enabled;
  out->joined = s_joined;
  out->channel = s_channel;
  out->pan_id = s_pan_id;
  int64_t now = esp_timer_get_time();
  out->pairing_remaining_s =
      (s_pair_until_us > now) ? (uint32_t)((s_pair_until_us - now) / 1000000) : 0;
  return ESP_OK;
}

esp_err_t zigbee_set_enabled(bool enabled)
{
  s_enabled = enabled;
  if (!enabled)
    s_pair_until_us = 0;
  persist();
  // ZB_TODO: start/stop the ZB task, commission or leave accordingly.
  ESP_LOGI(TAG, "Zigbee %s", enabled ? "enabled" : "disabled");
  broadcast_zigbee_config();
  return ESP_OK;
}

esp_err_t zigbee_start_pairing(uint32_t duration_s)
{
  if (!s_enabled)
    return ESP_ERR_INVALID_STATE;
  if (duration_s == 0 || duration_s > ZIG_PAIR_MAX_S)
    duration_s = 60;
  s_pair_until_us = esp_timer_get_time() + (int64_t)duration_s * 1000000;
  // ZB_TODO: BDB steering + Identify cluster blink for duration_s.
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
  // ZB_TODO: esp_zb_bdb_reset / leave with rejoin cleared.
  ESP_LOGI(TAG, "Left Zigbee network");
  broadcast_zigbee_config();
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
  // ZB_TODO: factory-reset the ZB stack (erase binding/groups/scenes).
  ESP_LOGI(TAG, "Zigbee factory reset");
  broadcast_zigbee_config();
  return ESP_OK;
}

void zigbee_report_state(void)
{
  if (!s_enabled || !s_joined)
    return;
  garage_state_t st;
  if (garage_controller_get_state(&st) != ESP_OK)
    return;
  // ZB_TODO: update cover position / onoff / lock / binary / occupancy
  // attributes from st (+ caps gating), debounced like MQTT.
  (void)st;
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
  xTaskCreate(zig_button_task, "zig_btn", 2048, NULL, 5, NULL);
}

#else // !CONFIG_SOC_IEEE802154_SUPPORTED: zero-cost stubs for 2MB classic builds

esp_err_t zigbee_init(void) { return ESP_OK; }

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
