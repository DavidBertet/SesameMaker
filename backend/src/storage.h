// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include "esp_err.h"

void setup_storage(void);

esp_err_t read_float(const char *key, float *value, float defaultValue);
esp_err_t write_float(const char *key, float value);

esp_err_t read_blob(const char *key, void *outValue, size_t *required_size);
esp_err_t write_blob(const char *key, const void *value, size_t required_size);
esp_err_t delete_blob(const char *key);

// Loads a blob of a known size. Returns ESP_ERR_NOT_FOUND if missing,
// ESP_ERR_INVALID_SIZE if stored size does not match.
esp_err_t read_blob_typed(const char *key, void *out_value, size_t expected_size);