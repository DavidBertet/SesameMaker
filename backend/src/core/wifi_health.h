// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Host-testable decision logic for the STA link-health watchdog (wifi.c).
// Kept free of ESP-IDF includes so backend/test/run_tests.sh can exercise it.

#ifndef WIFI_HEALTH_H
#define WIFI_HEALTH_H

// Outcome space of esp_wifi_sta_get_ap_info(), abstracted to a small enum so
// the decision code compiles and tests on the host.
typedef enum
{
    WIFI_HEALTH_ERR_OK = 0,       // Associated, RSSI read succeeded
    WIFI_HEALTH_ERR_NOT_ASSOC = 1, // ESP_ERR_WIFI_NOT_ASSOC: driver reports "connected" but association is gone (zombie link)
    WIFI_HEALTH_ERR_TRANSIENT = 2, // Any other failure (busy, not started, …)
} wifi_health_error_t;

typedef enum
{
    WIFI_HEALTH_ACTION_NONE = 0,
    // Force a fresh esp_wifi_connect(): the disconnect event was lost or never
    // fired, so the normal retry path never ran.
    WIFI_HEALTH_ACTION_REASSOC = 1,
} wifi_health_action_t;

wifi_health_action_t wifi_health_check(wifi_health_error_t get_err);

// The AP shows "connected" but answers nothing. Around -86 dBm a power-save
// STA can no longer reliably receive the AP's beacons and goes silent while
// the association lingers. Below this threshold the watchdog escalates logging.
#define WIFI_HEALTH_WEAK_RSSI_DBM (-75)

int wifi_health_is_weak_rssi(int rssi_dbm);

#endif
