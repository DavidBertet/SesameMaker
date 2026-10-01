// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ntp_sync.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "lwip/sys.h"

#include "wifi.h"
#include "storage.h"

static const char *TAG = "NTP_TIME";

static esp_err_t (*user_time_sync_callback)(void) = NULL;

static void get_and_print_time(void)
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    // Get current time
    time(&now);
    localtime_r(&now, &timeinfo);

    // Format and print the time
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current local time: %s", strftime_buf);

    // Also print Unix timestamp
    ESP_LOGI(TAG, "Unix timestamp: %lld", now);
}

// Callback function called when time is synchronized
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    get_and_print_time();

    if (user_time_sync_callback != NULL)
    {
        user_time_sync_callback();
    }
}

static void initialize_sntp(const char *server)
{
    ESP_LOGI(TAG, "Initializing SNTP");

    // Set the callback function for time synchronization
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // Set the operating mode
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Set the SNTP server
    esp_sntp_setservername(0, server);

    // Initialize SNTP service
    esp_sntp_init();
}

void time_config_get(char *server, size_t server_len, char *tz, size_t tz_len)
{
    if (server && server_len > 0)
    {
        if (read_str("ntp_server", server, server_len) != ESP_OK || server[0] == '\0')
            snprintf(server, server_len, "%s", NTP_SERVER);
    }
    if (tz && tz_len > 0)
    {
        if (read_str("tz", tz, tz_len) != ESP_OK || tz[0] == '\0')
            snprintf(tz, tz_len, "%s", NTP_TIMEZONE);
    }
}

void time_config_name(char *buf, size_t len)
{
    if (!buf || len == 0)
    {
        return;
    }
    if (read_str("tz_name", buf, len) != ESP_OK)
    {
        buf[0] = '\0';
    }
}

bool time_config_set(const char *server, const char *tz, const char *name)
{
    if (!server || server[0] == '\0' || strlen(server) > TIME_CFG_SERVER_MAX)
        return false;
    if (!tz || strlen(tz) > TIME_CFG_TZ_MAX)
        return false;
    if (name && strlen(name) > TIME_CFG_TZ_MAX)
        return false;
    // Empty TZ means UTC.
    if (write_str("ntp_server", server) != ESP_OK || write_str("tz", tz) != ESP_OK)
        return false;
    if (name)
    {
        write_str("tz_name", name);
    }
    // TZ applies to the C library immediately; the SNTP server takes
    // effect on the re-init below (esp_sntp_setservername is init-time).
    setenv("TZ", tz[0] ? tz : "UTC0", 1);
    tzset();
    esp_sntp_stop();
    initialize_sntp(server);
    ESP_LOGI(TAG, "Time config updated: server=%s tz=%s", server, tz[0] ? tz : "UTC0");
    return true;
}

static void ntp_time_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while (!wait_wifi_connection())
        ESP_LOGI(TAG, "Waiting for WiFi connection...");

    ESP_LOGI(TAG, "WiFi connected, starting NTP sync");

    char server[TIME_CFG_SERVER_MAX + 1];
    char tz[TIME_CFG_TZ_MAX + 1];
    time_config_get(server, sizeof(server), tz, sizeof(tz));

    setenv("TZ", tz[0] ? tz : "UTC0", 1);
    tzset();

    initialize_sntp(server);

    vTaskDelete(NULL);
}

void start_ntp_sync(void)
{
    xTaskCreate(ntp_time_task, "ntp_time_task", 4096, NULL, 5, NULL);
}

bool is_time_set(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    // Check if year is reasonable (greater than 2020)
    return (timeinfo.tm_year > (2020 - 1900));
}

void register_time_sync_callback(esp_err_t (*callback)(void))
{
    user_time_sync_callback = callback;
}