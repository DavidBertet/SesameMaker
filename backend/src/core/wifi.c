// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "wifi.h"

#include "ap_window.h"
#include "event_routes.h"
#include "storage.h"
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

#include "lwip/err.h"
#include "lwip/sys.h"

#define AP_WIFI_CHANNEL 10
#define MAX_AP_CONN 2

// Explicit-forget flag in the app "storage" NVS namespace (see storage.h).
// Set on user-initiated disconnect so a previously stored network is not
// resurrected on next reboot. Cleared on any new manual connect.
#define WIFI_FORGOT_KEY "wifi_forgot"

static const char *TAG = "wifi";

typedef struct
{
    char ssid[32];
    char password[64];
} wifi_credentials_t;

// WiFi event group
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
// Set when the fallback AP finishes starting. Only the AP-fallback paths
// wait on it (they don't need a STA link, just the portal being up).
#define WIFI_AP_STARTED_BIT BIT2

// Connection state
static bool s_wifi_connecting = false;
static bool s_wifi_connected = false;
static int s_retry_num = 0;
#define WIFI_MAXIMUM_RETRY 5

// Background reconnect timer so STA mode never gives up for good
#define WIFI_RECONNECT_PERIOD_US (30ULL * 1000000ULL) // 30 s
static esp_timer_handle_t s_reconnect_timer = NULL;

// One-shot timer closing the fallback-AP config window (AP_AUTO_OFF_S).
// Drops to STA-only: the portal is unreachable, STA keeps its background
// reconnect attempts. A missed window needs a reboot for a fresh one.
static esp_timer_handle_t s_ap_off_timer = NULL;

// Stations currently joined to the fallback AP. While > 0 the auto-off
// window is paused so a user mid-configuration is never kicked off; the
// last disconnect re-arms a fresh window.
static int s_ap_stations = 0;

static void ap_off_timer_callback(void *arg)
{
    if (s_ap_stations > 0)
    {
        // Join/stop race: a station landed after the timer was armed.
        // Never kick them — re-arm and let the countdown resume on leave.
        // No ESP_ERROR_CHECK here: this runs in esp_timer context, where an
        // abort would crash the timer task; log and carry on instead.
        ESP_LOGI(TAG, "%d station(s) still on AP, keeping it until they leave", s_ap_stations);
        esp_err_t err = esp_timer_start_once(s_ap_off_timer, (uint64_t)AP_AUTO_OFF_S * 1000000ULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to extend AP window: %s", esp_err_to_name(err));
        }
        return;
    }
    ESP_LOGW(TAG, "AP config window expired, disabling AP");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to disable AP: %s", esp_err_to_name(err));
    }
}

// Arm (or re-arm) the AP auto-off window. Called on every AP (re)start so an
// explicit disconnect after a previous auto-off still gets a fresh window.
static void arm_ap_auto_off(void)
{
    if (ap_window_action(AP_AUTO_OFF_S) != AP_WINDOW_TIMED)
    {
        return; // disabled (caller skips AP) or forever: no timer
    }
    if (s_ap_off_timer == NULL)
    {
        const esp_timer_create_args_t timer_args = {
            .callback = &ap_off_timer_callback,
            .name = "ap_auto_off",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_ap_off_timer));
    }
    if (esp_timer_is_active(s_ap_off_timer))
    {
        ESP_ERROR_CHECK(esp_timer_stop(s_ap_off_timer));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(s_ap_off_timer, (uint64_t)AP_AUTO_OFF_S * 1000000ULL));
    ESP_LOGI(TAG, "AP will auto-disable in %d s (missed it? reboot for a fresh window)", AP_AUTO_OFF_S);
}

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

static void wifi_set_forgotten(bool forgot)
{
    if (forgot)
    {
        write_u8(WIFI_FORGOT_KEY, 1);
    }
    else
    {
        delete_blob(WIFI_FORGOT_KEY);
    }
}

// Bring up the Wi-Fi driver in the given mode: init, coex handover, start.
// Netif creation and AP config stay with the caller.
static void wifi_start_driver(wifi_mode_t mode)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
    // Let coex own the Wi-Fi PS schedule so 802.15.4 RX can't starve
    // Wi-Fi entirely. Must be after init!
    ESP_ERROR_CHECK(esp_wifi_coex_pwr_configure(true));
#endif
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    ESP_ERROR_CHECK(esp_wifi_start());
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
    // Modem-sleep lets the arbiter time-slice the radio to 802.15.4.
    // Must be after start!
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#endif
}

static void setup_apsta(void)
{
    // AP_AUTO_OFF_S == 0: STA-only posture, never bring the AP up.
    if (ap_window_action(AP_AUTO_OFF_S) == AP_WINDOW_DISABLED)
    {
        ESP_LOGW(TAG, "Fallback AP disabled by config (AP_AUTO_OFF_S=0), staying STA-only");
        return;
    }

    // AP netif already exists (already up, or left over from a previous
    // auto-off): re-enable AP mode and re-arm the window.
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        arm_ap_auto_off();
        return;
    }

    esp_netif_create_default_wifi_ap();

    wifi_start_driver(WIFI_MODE_APSTA);

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

    arm_ap_auto_off();

    ESP_LOGI(TAG, "WiFi AP init finished. SSID:%s password:%s channel:%d", AP_WIFI_SSID, AP_WIFI_PASS, AP_WIFI_CHANNEL);
}

static esp_err_t setup_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_start_driver(WIFI_MODE_STA);

    ESP_LOGI(TAG, "WiFi STA init finished.");
    return ESP_OK;
}

static void on_ap_start(void *event_data)
{
    (void)event_data;
    // NOTE: deliberately does NOT set WIFI_CONNECTED_BIT — an AP start is
    // not a STA connection, and wifi_connect_task waits on that bit with no
    // timeout (a portal (re)start would fake a successful STA connect).
    ESP_LOGI(TAG, "WiFi AP started successfully!");
    xEventGroupSetBits(s_wifi_event_group, WIFI_AP_STARTED_BIT);
}

static void on_ap_staconnected(void *event_data)
{
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    if (s_ap_stations == 0 && s_ap_off_timer != NULL && esp_timer_is_active(s_ap_off_timer))
    {
        // First station on an empty portal: pause the countdown so a
        // user mid-configuration is never kicked off. As long as
        // someone is on the AP, the AP stays.
        esp_timer_stop(s_ap_off_timer);
        ESP_LOGI(TAG, "Portal occupied, auto-off paused");
    }
    s_ap_stations++;
}

static void on_ap_stadisconnected(void *event_data)
{
    wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    if (s_ap_stations > 0)
    {
        s_ap_stations--;
    }
    if (s_ap_stations == 0)
    {
        // Portal empty again: fresh window from here.
        arm_ap_auto_off();
    }
}

static void on_sta_start(void *event_data)
{
    (void)event_data;
    ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
    // Stored STA credentials (if any) live in NVS; connect with whatever is
    // there. An empty store skips the attempt entirely — blind retries with
    // no SSID only delay the AP fallback. Provisioning (portal, USB,
    // console) configures then connects through wifi_start_sta_connection
    // instead.
    if (is_wifi_setup())
    {
        esp_wifi_connect();
    }
    else
    {
        ESP_LOGI(TAG, "No stored credentials, skipping auto-connect");
    }
}

static void on_sta_disconnected(void *event_data)
{
    s_wifi_connected = false;
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
        s_wifi_connecting = false;
        // Quick retries exhausted: keep retrying in the background
        // so STA mode never loses access forever
        start_reconnect_timer();
    }
    ESP_LOGI(TAG, "Connect to the AP fail");
}

static void on_sta_got_ip(void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    bool was_reconnecting = stop_reconnect_timer();
    s_wifi_connected = true;
    s_wifi_connecting = false;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    if (was_reconnecting)
    {
        // Reconnected in the background: notify websocket clients
        ws_handle_wifi_status(NULL, 0);
    }
}

// Event bases (WIFI_EVENT, IP_EVENT) are externs, not constant expressions,
// so they can't key a file-scope table — one id-keyed table per base
// (ids are enum constants), routed through event_routes.h.
static const event_route_t s_wifi_routes[] = {
    {WIFI_EVENT_AP_START, on_ap_start},
    {WIFI_EVENT_AP_STACONNECTED, on_ap_staconnected},
    {WIFI_EVENT_AP_STADISCONNECTED, on_ap_stadisconnected},
    {WIFI_EVENT_STA_START, on_sta_start},
    {WIFI_EVENT_STA_DISCONNECTED, on_sta_disconnected},
};

static const event_route_t s_ip_routes[] = {
    {IP_EVENT_STA_GOT_IP, on_sta_got_ip},
};

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT)
    {
        event_routes_dispatch(s_wifi_routes, sizeof(s_wifi_routes) / sizeof(s_wifi_routes[0]), event_id, event_data);
    }
    else if (event_base == IP_EVENT)
    {
        event_routes_dispatch(s_ip_routes, sizeof(s_ip_routes) / sizeof(s_ip_routes[0]), event_id, event_data);
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
    // A stale FAIL_BIT from an older attempt would shortcut this wait
    // instantly — and spin any retry loop on it (seen: NTP log flood the
    // moment retries exhaust). Each wait starts clean and reports only this
    // window's outcome. CONNECTED stays sticky on purpose: still connected
    // means success right away.
    if (s_wifi_event_group)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(10000)); // Wait 10 seconds

    s_wifi_connecting = false;

    if (bits & WIFI_CONNECTED_BIT)
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Wait for the fallback AP to finish starting (portal usable). Only for the
// AP-fallback paths, which need the portal up rather than a STA link.
static void wait_ap_started(void)
{
    xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_AP_STARTED_BIT,
                        pdFALSE,
                        pdFALSE,
                        pdMS_TO_TICKS(10000)); // Wait 10 seconds
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

    // Credentials come from NVS (portal, USB or console provisioning) or
    // nowhere at all. With nothing stored, skip STA entirely and bring the
    // setup AP up immediately instead of burning retries + a 10 s wait on
    // an attempt that cannot succeed. Driver still starts (STA netif is
    // needed later when provisioning connects through
    // wifi_start_sta_connection).
    // Initialize WiFi
    esp_err_t ret = setup_sta();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize STA WiFi");
        setup_apsta(); // Fall back to AP mode
        return ret;
    }

    if (!is_wifi_setup())
    {
        ESP_LOGI(TAG, "No stored credentials, starting setup AP directly");
        setup_apsta();
        wait_ap_started();
        return ESP_FAIL;
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
        wait_ap_started();
        return ESP_FAIL;
    }
}

bool is_wifi_connected(void)
{
    return s_wifi_connected;
}

bool is_wifi_connecting(void)
{
    return s_wifi_connecting;
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

    s_wifi_connecting = true;

    wifi_credentials_t *creds = malloc(sizeof(wifi_credentials_t));
    if (!creds)
    {
        s_wifi_connecting = false;
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
        s_wifi_connecting = false;
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
    wait_ap_started();

    // Broadcast new values
    ws_handle_wifi_status(NULL, 0);
}