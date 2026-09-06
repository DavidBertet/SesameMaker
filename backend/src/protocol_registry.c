// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "protocol_registry.h"

#include "storage.h"

#include <string.h>

#define PROTOCOL_NVS_KEY "proto_id"

static protocol_id_t s_active = PROTOCOL_SECPLUS1;

esp_err_t protocol_registry_init(void)
{
    uint8_t stored = (uint8_t)PROTOCOL_SECPLUS1;
    size_t size = sizeof(stored);
    esp_err_t ret = read_blob(PROTOCOL_NVS_KEY, &stored, &size);
    if (ret == ESP_OK && size == sizeof(stored) &&
        protocol_id_valid(stored) && protocol_id_supported((protocol_id_t)stored))
    {
        s_active = (protocol_id_t)stored;
    }
    else
    {
        // Missing, corrupt, or not-yet-supported id: stay on secplus1 and
        // persist the default so the key always exists afterwards.
        s_active = PROTOCOL_SECPLUS1;
        uint8_t def = (uint8_t)s_active;
        write_blob(PROTOCOL_NVS_KEY, &def, sizeof(def));
    }
    return ESP_OK;
}

protocol_id_t protocol_registry_get(void)
{
    return s_active;
}

esp_err_t protocol_registry_set(protocol_id_t id)
{
    if (!protocol_id_valid((int)id) || !protocol_id_supported(id))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t v = (uint8_t)id;
    esp_err_t ret = write_blob(PROTOCOL_NVS_KEY, &v, sizeof(v));
    if (ret != ESP_OK)
    {
        return ret;
    }
    s_active = id;
    return ESP_OK;
}

protocol_caps_t protocol_registry_caps(void)
{
    return protocol_caps_for(s_active);
}
