// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "wifi.h"

#include "ws_wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/event_groups.h"

#include "constants.h"

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASSWORD
#define DEFAULT_WIFI_PASSWORD ""
#endif
#ifndef DEFAULT_WIFI_GEN
#define DEFAULT_WIFI_GEN ""
#endif

#include "lwip/err.h"
#include "lwip/sys.h"

#define AP_WIFI_SSID "SesameMaker"
#define AP_WIFI_PASS "StopNow!"
#define AP_WIFI_CHANNEL 10
#define MAX_AP_CONN 2

// Explicit-forget flag in the app "storage" NVS namespace. Set on user-initiated
// disconnect so build-time DEFAULT_WIFI_* is not resurrected on next reboot.
// Cleared on any new manual connect.
#define WIFI_STORAGE_NS "storage"
#define WIFI_FORGOT_KEY "wifi_forgot"
// Last applied DEFAULT_WIFI_GEN. A fresh `install --wifi-ssid` bumps the baked
// gen, so a gen mismatch means "new provisioning, override once".
#define WIFI_GEN_KEY "wifi_gen"
#define WIFI_GEN_MAX_LEN 16

static const char *TAG = "wifi";

typedef struct
{
    char ssid[32];
    char password[64];
} wifi_credentials_t;

// WiFi event group
static EventGroupHandle_t s_wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

// Connection state
static bool wifi_connecting = false;
static bool wifi_connected = false;
// True while setup_wifi() is about to apply build-time defaults. The
// STA_START handler must skip its auto-connect then: set_config while a
// connect is in flight aborts with ESP_ERR_WIFI_STATE (0x3006).
static bool s_provision_pending = false;
static int s_retry_num = 0;
static const int WIFI_MAXIMUM_RETRY = 5;

// Background reconnect timer so STA mode never gives up for good
#define WIFI_RECONNECT_PERIOD_US (30ULL * 1000000ULL) // 30 s
static esp_timer_handle_t s_reconnect_timer = NULL;

static void start_reconnect_timer(void);

static void reconnect_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "STA reconnect timer fired, retrying connection");
    // Allow a new burst of quick retries after each timer tick
    s_retry_num = 0;
    esp_wifi_connect();
}

static void start_reconnect_timer(void)
{
    if (s_reconnect_timer == NULL)
    {
        const esp_timer_create_args_t timer_args = {
            .callback = &reconnect_timer_callback,
            .name = "sta_reconnect",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));
    }

    if (esp_timer_is_active(s_reconnect_timer))
    {
        return;
    }

    ESP_ERROR_CHECK(esp_timer_start_periodic(s_reconnect_timer, WIFI_RECONNECT_PERIOD_US));
    ESP_LOGI(TAG, "STA reconnect timer started (every %llu s)", WIFI_RECONNECT_PERIOD_US / 1000000ULL);
}

static bool stop_reconnect_timer(void)
{
    if (s_reconnect_timer == NULL || !esp_timer_is_active(s_reconnect_timer))
    {
        return false;
    }

    esp_timer_stop(s_reconnect_timer);
    ESP_LOGI(TAG, "STA reconnect timer stopped");
    return true;
}

static bool wifi_is_forgotten(void)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_STORAGE_NS, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }
    uint8_t forgot = 0;
    esp_err_t err = nvs_get_u8(handle, WIFI_FORGOT_KEY, &forgot);
    nvs_close(handle);
    return err == ESP_OK && forgot != 0;
}

static void wifi_set_forgotten(bool forgot)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_STORAGE_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open storage NVS for wifi forgot flag");
        return;
    }
    if (forgot)
    {
        nvs_set_u8(handle, WIFI_FORGOT_KEY, 1);
    }
    else
    {
        nvs_erase_key(handle, WIFI_FORGOT_KEY);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

static bool wifi_gen_applied(const char *baked_gen)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_STORAGE_NS, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }
    char stored[WIFI_GEN_MAX_LEN + 1] = {0};
    size_t len = sizeof(stored);
    esp_err_t err = nvs_get_str(handle, WIFI_GEN_KEY, stored, &len);
    nvs_close(handle);
    return err == ESP_OK && strcmp(stored, baked_gen) == 0;
}

static void wifi_mark_gen_applied(const char *baked_gen)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_STORAGE_NS, NVS_READWRITE, &handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open storage NVS for wifi gen flag");
        return;
    }
    nvs_set_str(handle, WIFI_GEN_KEY, baked_gen);
    nvs_commit(handle);
    nvs_close(handle);
}

static void wifi_apply_build_defaults(void)
{
    ESP_LOGI(TAG, "Applying build-time default SSID");
    wifi_config_t default_config = {0};
    strncpy((char *)default_config.sta.ssid, DEFAULT_WIFI_SSID,
            sizeof(default_config.sta.ssid) - 1);
    strncpy((char *)default_config.sta.password, DEFAULT_WIFI_PASSWORD,
            sizeof(default_config.sta.password) - 1);
    default_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    default_config.sta.pmf_cfg.capable = true;
    default_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &default_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void setup_apsta(void)
{
    // Check if AP is already setup
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
    {
        return;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_WIFI_SSID,
            .ssid_len = strlen(AP_WIFI_SSID),
            .channel = AP_WIFI_CHANNEL,
            .password = AP_WIFI_PASS,
            .max_connection = MAX_AP_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    if (strlen(AP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_LOGI(TAG, "WiFi AP init finished. SSID:%s password:%s channel:%d", AP_WIFI_SSID, AP_WIFI_PASS, AP_WIFI_CHANNEL);
}

static esp_err_t setup_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA init finished.");
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        printf("WiFi AP started successfully!\n");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        if (s_provision_pending)
        {
            // setup_wifi() applies build-time defaults right after start and
            // connects itself; an auto-connect here would put set_config in a
            // connecting state and abort the boot.
            return;
        }
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_connected = false;
        const wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED reason %d", event->reason);
        // Only device-initiated disconnects (we called esp_wifi_disconnect())
        // should skip retrying. Any network-side drop - beacon timeout,
        // handshake timeout, auth/assoc fail, AP gone - must be retried.
        // The old `reason < 200` check wrongly swallowed real drop reasons
        // (e.g. HANDSHAKE_TIMEOUT=67, 4WAY_HANDSHAKE_TIMEOUT=15) and left
        // neither the quick retry loop nor the background timer running,
        // so STA mode silently stopped reconnecting for good.
        if (event->reason == WIFI_REASON_ASSOC_LEAVE ||
            event->reason == WIFI_REASON_AUTH_LEAVE)
        {
            return;
        }

        if (s_retry_num < WIFI_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            wifi_connecting = false;
            // Quick retries exhausted: keep retrying in the background
            // so STA mode never loses access forever
            start_reconnect_timer();
        }
        ESP_LOGI(TAG, "Connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        bool was_reconnecting = stop_reconnect_timer();
        wifi_connected = true;
        wifi_connecting = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (was_reconnecting)
        {
            // Reconnected in the background: notify websocket clients
            ws_handle_wifi_status(NULL, 0);
        }
    }
}

static void wifi_connect_task(void *pvParameters)
{
    wifi_credentials_t *creds = (wifi_credentials_t *)pvParameters;

    ESP_LOGI(TAG, "Starting WiFi connection to SSID: %s", creds->ssid);

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, creds->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, creds->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for connection result
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    // Send connection result
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", creds->ssid);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", creds->ssid);
    }

    // Broadcast new values
    ws_handle_wifi_status(NULL, 0);

    // Clean up
    free(creds);
    vTaskDelete(NULL);
}

bool wait_wifi_connection(void)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(10000)); // Wait 10 seconds

    wifi_connecting = false;

    if (bits & WIFI_CONNECTED_BIT)
    {
        return true;
    }
    else
    {
        return false;
    }
}

esp_err_t setup_wifi(void)
{
    ESP_LOGI(TAG, "Attempting auto-connect with stored credentials");

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &event_handler,
                                               NULL));

    if (s_wifi_event_group == NULL)
    {
        s_wifi_event_group = xEventGroupCreate();
    }

    // Decide build-time provisioning BEFORE starting WiFi (NVS reads only).
    // The STA_START handler auto-connects, so the decision must be visible to
    // it before setup_sta() starts the driver.
    //   • baked DEFAULT_WIFI_GEN differs from last applied → fresh
    //     `install --wifi-ssid`: override stored creds + forget flag once.
    //   • NVS empty and never explicitly forgotten → first boot fallback.
    //   • Otherwise (gen equal) → respect runtime choices: UI-chosen network
    //     or explicit disconnect.
    bool provision = false;
    if (strlen(DEFAULT_WIFI_SSID) > 0)
    {
        if (strlen(DEFAULT_WIFI_GEN) > 0 && !wifi_gen_applied(DEFAULT_WIFI_GEN))
        {
            ESP_LOGI(TAG, "New build-time WiFi provisioning, overriding stored credentials");
            wifi_set_forgotten(false);
            wifi_mark_gen_applied(DEFAULT_WIFI_GEN);
            provision = true;
        }
        else if (!is_wifi_setup() && !wifi_is_forgotten())
        {
            ESP_LOGI(TAG, "No stored credentials, trying build-time default SSID");
            if (strlen(DEFAULT_WIFI_GEN) > 0)
            {
                wifi_mark_gen_applied(DEFAULT_WIFI_GEN);
            }
            provision = true;
        }
    }
    s_provision_pending = provision;

    // Initialize WiFi
    esp_err_t ret = setup_sta();
    if (ret != ESP_OK)
    {
        s_provision_pending = false;
        ESP_LOGE(TAG, "Failed to initialize STA WiFi");
        setup_apsta(); // Fall back to AP mode
        return ret;
    }

    if (provision)
    {
        // No connect in flight (handler skipped it): safe to configure.
        wifi_apply_build_defaults();
        s_provision_pending = false;
    }
    // If connection fails, we'll get a disconnect event

    // Wait a bit to see if connection succeeds
    bool is_connected = wait_wifi_connection();

    if (is_connected)
    {
        ESP_LOGI(TAG, "Auto-connect successful");
        return ESP_OK;
    }
    else
    {
        ESP_LOGW(TAG, "Auto-connect failed or timed out, starting AP mode");
        setup_apsta(); // Fall back to AP mode
        wait_wifi_connection();
        return ESP_FAIL;
    }
}

bool is_wifi_connected(void)
{
    return wifi_connected;
}

bool is_wifi_connecting(void)
{
    return wifi_connecting;
}

bool is_wifi_setup(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("nvs.net80211", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        return false;
    }

    // Check if WiFi SSID is stored
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, "sta.ssid", NULL, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK && required_size > 0)
    {
        return true;
    }

    return false;
}

esp_err_t wifi_start_sta_connection(const char *ssid, const char *password)
{
    // Manual attempt supersedes background reconnects
    stop_reconnect_timer();

    // New explicit connect clears a previous forget so defaults matter no more
    // (stored NVS creds take precedence from here on).
    wifi_set_forgotten(false);

    wifi_connecting = true;

    wifi_credentials_t *creds = malloc(sizeof(wifi_credentials_t));
    if (!creds)
    {
        wifi_connecting = false;
        return ESP_ERR_NO_MEM;
    }
    strncpy(creds->ssid, ssid, sizeof(creds->ssid) - 1);
    strncpy(creds->password, password, sizeof(creds->password) - 1);
    creds->ssid[sizeof(creds->ssid) - 1] = '\0';
    creds->password[sizeof(creds->password) - 1] = '\0';

    // Reset retry counter and event group
    s_retry_num = 0;
    if (s_wifi_event_group)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }

    // Create task to handle WiFi connection
    BaseType_t result = xTaskCreate(wifi_connect_task, "wifi_connect_task", 4096, creds, 5, NULL);
    if (result != pdPASS)
    {
        wifi_connecting = false;
        free(creds);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void wifi_stop_sta_connection(void)
{
    // Intentional disconnect: don't let the reconnect timer bring it back
    stop_reconnect_timer();

    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to disconnect from WiFi: %s", esp_err_to_name(ret));
    }

    wifi_config_t wifi_config = {0}; // Zero out the config
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to clear WiFi config: %s", esp_err_to_name(ret));
    }

    nvs_handle_t nvs_handle;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &nvs_handle) == ESP_OK)
    {
        nvs_erase_all(nvs_handle); // Clear all WiFi related NVS data
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    // Remember the explicit forget so build-time defaults stay dormant
    // across reboots until the user connects again.
    wifi_set_forgotten(true);

    // Restart in AP mode
    setup_apsta();
    wait_wifi_connection();

    // Broadcast new values
    ws_handle_wifi_status(NULL, 0);
}