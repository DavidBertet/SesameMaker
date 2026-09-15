// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit accessory transport (core, no product knowledge).

#include "homekit.h"

#include "storage.h"
#include "wifi.h"
#include "ws_homekit.h"

#include "hap.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "HOMEKIT";

// NVS home for the setup code (hk_storage partition, "homekit" namespace).
#define HK_NVS_PARTITION "hk_storage"
#define HK_NVS_NAMESPACE "homekit"
#define HK_SETUP_CODE_KEY "setup_code"
#define HK_SETUP_ID_KEY "setup_id"
#define HK_SETUP_CODE_LEN 12 // "XXX-XX-XXX" + nul
#define HK_SETUP_ID_LEN 5    // 4 chars + nul
#define HK_PAYLOAD_BUF 64

// Max time to let Wi-Fi associate before starting HAP. Bounded so a down
// AP never blocks the accessory forever (same pattern as the ZB task).
#define HK_WIFI_WAIT_MAX_MS 15000
#define HK_WIFI_WAIT_POLL_MS 500
#define HK_TASK_STACK 8192
#define HK_TASK_PRIO 5

static homekit_device_t s_device;
static bool s_device_bound = false;
static bool s_task_running = false;
static volatile bool s_ready = false;
static bool s_enabled = false; // persisted in default NVS (see below)
static char s_setup_code[HK_SETUP_CODE_LEN] = "";
static char s_setup_id[HK_SETUP_ID_LEN] = "";
static int s_category_id = 0;
static char s_payload_buf[HK_PAYLOAD_BUF] = "";

// Enabled flag lives in default NVS next to the Zigbee/MQTT config blobs
// (hk_storage holds HAP pairing data, not our own settings).
#define HK_CFG_KEY "hk_cfg"
typedef struct
{
  uint8_t enabled;
  uint8_t _reserved[3];
} hk_persist_t;

static void persist_enabled(void)
{
  hk_persist_t p;
  memset(&p, 0, sizeof(p));
  p.enabled = s_enabled ? 1 : 0;
  write_blob(HK_CFG_KEY, &p, sizeof(p));
}

static void load_enabled(void)
{
  size_t n = 0;
  hk_persist_t p;
  memset(&p, 0, sizeof(p));
  if (read_blob(HK_CFG_KEY, NULL, &n) == ESP_OK && n == sizeof(p) &&
      read_blob(HK_CFG_KEY, &p, &n) == ESP_OK)
  {
    s_enabled = p.enabled != 0;
  }
  else
  {
    persist_enabled();
  }
}

// Generate an 8-digit setup code formatted "XXX-XX-XXX".
static void gen_setup_code(char *out)
{
  uint32_t r = esp_random();
  snprintf(out, HK_SETUP_CODE_LEN, "%03lu-%02lu-%03lu",
           (unsigned long)(r % 1000), (unsigned long)((r / 1000) % 100),
           (unsigned long)((r / 100000) % 1000));
}

// Generate a 4-char base36 setup id (distinguishes our accessories).
static void gen_setup_id(char *out)
{
  static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint32_t r = esp_random();
  for (int i = 0; i < 4; i++)
  {
    out[i] = alphabet[r % 36];
    r /= 36;
  }
  out[4] = '\0';
}

// Load the setup code/id from hk_storage, generating + persisting on
// first boot (factory reset wipes them, producing a fresh code).
static void load_or_gen_setup(void)
{
  nvs_handle_t h;
  esp_err_t ret = nvs_open_from_partition(HK_NVS_PARTITION, HK_NVS_NAMESPACE,
                                          NVS_READWRITE, &h);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "nvs open %s failed: %s", HK_NVS_PARTITION,
             esp_err_to_name(ret));
    goto fallback;
  }
  size_t code_len = sizeof(s_setup_code);
  size_t id_len = sizeof(s_setup_id);
  bool have_code = nvs_get_str(h, HK_SETUP_CODE_KEY, s_setup_code, &code_len) == ESP_OK;
  bool have_id = nvs_get_str(h, HK_SETUP_ID_KEY, s_setup_id, &id_len) == ESP_OK;
  if (!have_code)
  {
    gen_setup_code(s_setup_code);
    nvs_set_str(h, HK_SETUP_CODE_KEY, s_setup_code);
  }
  if (!have_id)
  {
    gen_setup_id(s_setup_id);
    nvs_set_str(h, HK_SETUP_ID_KEY, s_setup_id);
  }
  if (!have_code || !have_id)
    nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "Setup code %s id %s (%s)", s_setup_code, s_setup_id,
           (!have_code || !have_id) ? "generated" : "stored");
  return;

fallback:
  // Storage broken: boot pairable with a random code rather than dead.
  gen_setup_code(s_setup_code);
  gen_setup_id(s_setup_id);
  ESP_LOGW(TAG, "Using ephemeral setup code %s", s_setup_code);
}

static void hap_event_logger(hap_event_t event, void *data)
{
  (void)data;
  if (event == HAP_EVENT_PAIRING_STARTED)
    ESP_LOGI(TAG, "Pairing started");
  else if (event == HAP_EVENT_PAIRING_ABORTED)
    ESP_LOGW(TAG, "Pairing aborted (timeout or wrong code)");
  else if (event == HAP_EVENT_CTRL_PAIRED)
  {
    ESP_LOGI(TAG, "Controller paired");
    broadcast_homekit_config();
  }
  else if (event == HAP_EVENT_CTRL_UNPAIRED)
  {
    ESP_LOGI(TAG, "Controller unpaired");
    broadcast_homekit_config();
  }
}

static void homekit_task(void *arg)
{
  (void)arg;
  if (!s_device_bound || !s_device.build_accessory)
  {
    ESP_LOGE(TAG, "No device registered, HAP not starting");
    s_task_running = false;
    vTaskDelete(NULL);
  }
  if (!s_enabled)
  {
    ESP_LOGI(TAG, "Disabled, HAP not starting");
    s_task_running = false;
    vTaskDelete(NULL);
  }
  s_category_id = s_device.category_id;

  if (!is_wifi_setup())
  {
    ESP_LOGI(TAG, "No STA creds (AP mode), skipping wifi wait");
  }
  else
  {
    uint32_t waited_ms = 0;
    while (!is_wifi_connected() && waited_ms < HK_WIFI_WAIT_MAX_MS)
    {
      vTaskDelay(pdMS_TO_TICKS(HK_WIFI_WAIT_POLL_MS));
      waited_ms += HK_WIFI_WAIT_POLL_MS;
    }
    ESP_LOGI(TAG, "wifi_connected=%d after %lums", (int)is_wifi_connected(),
             (unsigned long)waited_ms);
  }
  if (!s_enabled)
  {
    // Disabled while waiting for Wi-Fi: stand down.
    ESP_LOGI(TAG, "Disabled while waiting, HAP not starting");
    s_task_running = false;
    vTaskDelete(NULL);
  }

  load_or_gen_setup();
  hap_set_setup_code(s_setup_code);
  hap_set_setup_id(s_setup_id);

  if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS)
  {
    ESP_LOGE(TAG, "hap_init failed, HAP dead");
    s_task_running = false;
    vTaskDelete(NULL);
  }
  s_device.build_accessory();
  hap_register_event_handler(hap_event_logger);
  if (hap_start() != HAP_SUCCESS)
  {
    // Never swallow this: a dead hap_start() looks exactly like a
    // network problem (joined Wi-Fi, no mDNS, iPhone spins forever).
    ESP_LOGE(TAG, "hap_start failed, HAP dead (check LWIP sockets/mdns)");
    s_task_running = false;
    vTaskDelete(NULL);
  }
  s_ready = true;
  ESP_LOGI(TAG, "HAP started");
  broadcast_homekit_config();
  s_task_running = false;
  vTaskDelete(NULL);
}

void homekit_register_device(const homekit_device_t *dev)
{
  if (!dev)
    return;
  s_device = *dev;
  s_device_bound = true;
}

void homekit_start(void)
{
  load_enabled();
  if (!s_enabled || s_task_running || s_ready)
    return;
  s_task_running = true;
  xTaskCreate(homekit_task, "homekit", HK_TASK_STACK, NULL, HK_TASK_PRIO, NULL);
}

esp_err_t homekit_set_enabled(bool enabled)
{
  load_enabled();
  if (s_enabled == enabled)
  {
    if (enabled && !s_ready)
      homekit_start(); // retry a previously failed start
    return ESP_OK;
  }
  s_enabled = enabled;
  persist_enabled();
  if (!enabled)
  {
    if (s_ready)
    {
      hap_stop();
      s_ready = false;
    }
    ESP_LOGI(TAG, "HomeKit disabled");
  }
  else
  {
    ESP_LOGI(TAG, "HomeKit enabled, starting stack");
    homekit_start();
  }
  broadcast_homekit_config();
  return ESP_OK;
}

bool homekit_enabled(void)
{
  return s_enabled;
}

bool homekit_started(void)
{
  return s_ready;
}

bool homekit_is_paired(void)
{
  if (!s_ready)
    return false;
  return hap_get_paired_controller_count() > 0;
}

const char *homekit_setup_payload(void)
{
  if (!s_ready || s_setup_code[0] == '\0')
    return NULL;
  if (homekit_is_paired())
    return NULL;
  char *p = esp_hap_get_setup_payload(s_setup_code, s_setup_id, false,
                                      (hap_cid_t)s_category_id);
  if (!p)
    return NULL;
  snprintf(s_payload_buf, sizeof(s_payload_buf), "%s", p);
  free(p);
  return s_payload_buf;
}
