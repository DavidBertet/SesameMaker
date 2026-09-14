// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "storage.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *NVS_NAMESPACE = "storage";
static const char *TAG = "STORAGE";

// Long term storage that survives restart

// Erase + reinit a named NVS partition when it is full or corrupt.
// Never aborts: logs and returns the error so boot can continue degraded
// instead of panic-looping on bad flash.
static esp_err_t init_nvs(const char *label)
{
  esp_err_t ret = nvs_flash_init_partition(label);
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    esp_err_t erase = nvs_flash_erase_partition(label);
    if (erase != ESP_OK)
    {
      ESP_LOGE(TAG, "erase %s failed: %s", label, esp_err_to_name(erase));
      return erase;
    }
    ret = nvs_flash_init_partition(label);
  }
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "init %s failed: %s", label, esp_err_to_name(ret));
  }
  return ret;
}

esp_err_t setup_storage(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    esp_err_t erase = nvs_flash_erase();
    if (erase != ESP_OK)
    {
      ESP_LOGE(TAG, "erase nvs failed: %s", esp_err_to_name(erase));
      return erase;
    }
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "init nvs failed: %s", esp_err_to_name(ret));
    return ret;
  }
  // ESP Zigbee SDK v2.x keeps the stack's network/security dataset in its
  // own NVS partition ("zb_storage", data/nvs) so ZBOSS growth can never
  // exhaust the partition holding Wi-Fi + app settings. Same pattern for
  // HomeKit ("hk_storage"): pairing database + setup code, isolated.
  esp_err_t ret_zb = init_nvs("zb_storage");
  esp_err_t ret_hk = init_nvs("hk_storage");
  return (ret_zb != ESP_OK) ? ret_zb : ret_hk;
}

esp_err_t read_float(const char *key, float *value, float defaultValue)
{
  *value = defaultValue;
  size_t required_size = 4;
  return read_blob(key, (void *)value, &required_size);
}

esp_err_t write_float(const char *key, float value)
{
  return write_blob(key, (void *)&value, sizeof(float));
}

esp_err_t read_blob(const char *key, void *outValue, size_t *required_size)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    return ret;
  }
  ret = nvs_get_blob(nvs_handle, key, outValue, required_size);
  nvs_close(nvs_handle);
  return ret;
}

esp_err_t write_blob(const char *key, const void *value, size_t required_size)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    return ret;
  }
  ret = nvs_set_blob(nvs_handle, key, value, required_size);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(nvs_handle);
  }

  nvs_close(nvs_handle);
  return ret;
}

esp_err_t delete_blob(const char *key)
{
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK)
  {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    return ret;
  }
  ret = nvs_erase_key(nvs_handle, key);
  if (ret == ESP_OK)
  {
    ret = nvs_commit(nvs_handle);
  }

  nvs_close(nvs_handle);
  return ret;
}

esp_err_t read_blob_typed(const char *key, void *out_value, size_t expected_size)
{
  size_t stored_size = 0;
  esp_err_t ret = read_blob(key, NULL, &stored_size);
  if (ret != ESP_OK || stored_size != expected_size)
  {
    return (ret == ESP_OK) ? ESP_ERR_INVALID_SIZE : ret;
  }
  return read_blob(key, out_value, &stored_size);
}