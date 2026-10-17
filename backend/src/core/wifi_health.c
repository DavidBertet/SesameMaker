// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "wifi_health.h"

wifi_health_action_t wifi_health_check(wifi_health_error_t get_err)
{
    switch (get_err)
    {
    case WIFI_HEALTH_ERR_OK:
        return WIFI_HEALTH_ACTION_NONE;
    case WIFI_HEALTH_ERR_NOT_ASSOC:
        return WIFI_HEALTH_ACTION_REASSOC;
    case WIFI_HEALTH_ERR_TRANSIENT:
    default:
        return WIFI_HEALTH_ACTION_NONE;
    }
}

int wifi_health_is_weak_rssi(int rssi_dbm)
{
    return rssi_dbm <= WIFI_HEALTH_WEAK_RSSI_DBM;
}
