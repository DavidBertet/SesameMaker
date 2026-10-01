// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Left-open escalation wiring (app): NVS config, 1 s watchdog task polling
// the controller state, endpoint handlers. See left_open.h for the machine.

#include "ws_left_open.h"

#include "garage_controller.h"
#include "left_open.h"
#include "mqtt.h"
#include "storage.h"
#include "websocket.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LEFT_OPEN";

#define LEFT_OPEN_CFG_KEY "leftopen_cfg"
#define LEFT_OPEN_TASK_STACK 3072
#define LEFT_OPEN_TASK_PRIO 5

static TaskHandle_t s_task;
static left_open_state_t s_state;
// Set when we commanded an auto-close; cleared (with a closed event) on the
// first poll showing the door shut — closing the notify loop.
static bool s_close_commanded = false;
// Set on the first BLINK poll, cleared on close: restores the opener light
// to its pre-blink state after the auto-close (lost on reboot — best effort).
static bool s_blinked = false;

static void load_cfg(left_open_cfg_t *cfg)
{
    size_t stored = 0;
    left_open_cfg_t disk;
    if (read_blob(LEFT_OPEN_CFG_KEY, NULL, &stored) == ESP_OK && stored == sizeof(disk) &&
        read_blob(LEFT_OPEN_CFG_KEY, &disk, &stored) == ESP_OK && left_open_valid(&disk))
    {
        *cfg = disk;
        return;
    }
    left_open_defaults(cfg);
}

static bool save_cfg(const left_open_cfg_t *cfg)
{
    return left_open_valid(cfg) && write_blob(LEFT_OPEN_CFG_KEY, cfg, sizeof(*cfg)) == ESP_OK;
}

// Best-effort event POST (fire-and-forget, 5 s cap). Failures only log:
// notification must never break the watchdog loop. Human-readable body for
// ntfy-style readers (the phone shows it verbatim); machine routing rides
// along in X-Title / X-Tags headers.
static void webhook_post(const char *url, const char *event, uint32_t elapsed_s)
{
    if (!url || !url[0])
    {
        return;
    }
    unsigned mins = elapsed_s / 60;
    unsigned secs = elapsed_s % 60;
    const char *title = "Door event";
    const char *tags = "door,info";
    char body[160];
    if (strcmp(event, "warn") == 0)
    {
        title = "Door left open";
        tags = "door,warning";
        snprintf(body, sizeof(body), "The garage door has been open for %u:%02u.", mins, secs);
    }
    else if (strcmp(event, "auto_close") == 0)
    {
        title = "Auto-closing the door";
        tags = "door,warning";
        snprintf(body, sizeof(body), "Left open for %u:%02u — closing now.", mins, secs);
    }
    else if (strcmp(event, "blocked") == 0)
    {
        title = "Auto-close blocked";
        tags = "door,warning";
        snprintf(body, sizeof(body), "Obstruction detected after %u:%02u open — the door stays open.",
                 mins, secs);
    }
    else if (strcmp(event, "closed") == 0)
    {
        title = "Door closed";
        tags = "door,white_check_mark";
        snprintf(body, sizeof(body), "The auto-closed door is now shut.");
    }
    else
    {
        snprintf(body, sizeof(body), "Door event %s after %u:%02u.", event, mins, secs);
    }
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_header(client, "X-Title", title);
    esp_http_client_set_header(client, "X-Tags", tags);
    esp_http_client_set_post_field(client, body, strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Webhook %s failed: %s", event, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void emit_event(const char *event, uint32_t elapsed_s, const left_open_cfg_t *cfg)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"type\":\"door_event\",\"event\":\"%s\",\"elapsed_s\":%u}", event,
             (unsigned)elapsed_s);
    broadcast_message(json);
    mqtt_publish("left_open", json, 0, false);
    webhook_post(cfg->webhook, event, elapsed_s);
}

static void left_open_task(void *arg)
{
    (void)arg;
    garage_state_t gs;
    while (1)
    {
        left_open_cfg_t cfg;
        load_cfg(&cfg);
        bool open = false;
        bool obstructed = false;
        bool light_on = false;
        bool light_capable = false;
        if (garage_controller_get_state(&gs) == ESP_OK)
        {
            open = gs.door_state == SECPLUS1_DOOR_OPEN;
            obstructed = gs.obstruction;
            light_capable = gs.caps.light;
            light_on = gs.light_state == GARAGE_LIGHT_ON;
        }
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint8_t actions =
            left_open_step(&s_state, now_ms, open, obstructed, light_on, light_capable, &cfg);
        uint32_t elapsed_s =
            s_state.open ? (now_ms - s_state.open_since_ms) / 1000 : 0;
        if (!open && s_close_commanded)
        {
            s_close_commanded = false;
            ESP_LOGI(TAG, "Auto-closed door confirmed shut");
            emit_event("closed", 0, &cfg);
        }
        if (!open)
        {
            s_blinked = false;
        }
        if (actions & LEFT_OPEN_BLINK)
        {
            // 1 Hz warning blink while the window runs (dry-contact never
            // gets here: the machine skips the window without a light).
            s_blinked = true;
            garage_controller_light_action(((now_ms / 1000) % 2) ? "on" : "off");
        }
        if (actions & LEFT_OPEN_WARN)
        {
            ESP_LOGW(TAG, "Door open %us, warning", (unsigned)elapsed_s);
            emit_event("warn", elapsed_s, &cfg);
        }
        if (actions & LEFT_OPEN_BLOCKED)
        {
            ESP_LOGW(TAG, "Auto-close blocked by obstruction (%us open)", (unsigned)elapsed_s);
            emit_event("blocked", elapsed_s, &cfg);
        }
        if (actions & LEFT_OPEN_CLOSE)
        {
            ESP_LOGW(TAG, "Auto-closing after %us open", (unsigned)elapsed_s);
            if (s_blinked)
            {
                s_blinked = false;
                garage_controller_light_action(s_state.light_was_on ? "on" : "off");
            }
            if (garage_controller_door_action("close") == ESP_OK)
            {
                s_close_commanded = true;
                emit_event("auto_close", elapsed_s, &cfg);
            }
            else
            {
                emit_event("blocked", elapsed_s, &cfg);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void left_open_start(void)
{
    if (!s_task)
    {
        xTaskCreate(left_open_task, "left_open", LEFT_OPEN_TASK_STACK, NULL, LEFT_OPEN_TASK_PRIO,
                    &s_task);
        ESP_LOGI(TAG, "Watchdog started");
    }
}

static void send_config(int sockfd)
{
    left_open_cfg_t cfg;
    load_cfg(&cfg);
    char json[448];
    snprintf(json, sizeof(json),
             "{\"type\":\"left_open_config\",\"enabled\":%s,\"warn_s\":%u,\"close_s\":%u,"
             "\"blink_light\":%s,\"webhook\":\"%.127s\",\"open\":%s,\"elapsed_s\":%u}",
             cfg.enabled ? "true" : "false", (unsigned)cfg.warn_s, (unsigned)cfg.close_s,
             cfg.blink_light ? "true" : "false", cfg.webhook,
             s_state.open ? "true" : "false",
             s_state.open ? (unsigned)((uint32_t)(esp_timer_get_time() / 1000) -
                                       s_state.open_since_ms) /
                                1000
                          : 0);
    send_message_sockfd(json, sockfd);
}

void ws_handle_get_left_open(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_left_open");
    send_config(sockfd);
}

void ws_handle_set_left_open(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "set_left_open");
    left_open_cfg_t cfg;
    load_cfg(&cfg);
    cJSON *en = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(en))
    {
        cfg.enabled = cJSON_IsTrue(en);
    }
    cJSON *w = cJSON_GetObjectItem(root, "warn_s");
    if (cJSON_IsNumber(w))
    {
        cfg.warn_s = (uint32_t)w->valueint;
    }
    cJSON *c = cJSON_GetObjectItem(root, "close_s");
    if (cJSON_IsNumber(c))
    {
        cfg.close_s = (uint32_t)c->valueint;
    }
    cJSON *bl = cJSON_GetObjectItem(root, "blink_light");
    if (cJSON_IsBool(bl))
    {
        cfg.blink_light = cJSON_IsTrue(bl);
    }
    cJSON *wh = cJSON_GetObjectItem(root, "webhook");
    if (cJSON_IsString(wh))
    {
        snprintf(cfg.webhook, sizeof(cfg.webhook), "%s", wh->valuestring);
    }
    char json[160];
    if (!save_cfg(&cfg))
    {
        bool needs_hook = cfg.enabled && cfg.warn_s > 0 && !cfg.webhook[0];
        snprintf(json, sizeof(json),
                 "{\"type\":\"left_open_saved\",\"success\":false,\"message\":\"%.100s\"}",
                 needs_hook ? "Webhook URL is required when warnings are on"
                            : "Invalid config");
        send_message_sockfd(json, sockfd);
        return;
    }
    snprintf(json, sizeof(json), "{\"type\":\"left_open_saved\",\"success\":true}");
    send_message_sockfd(json, sockfd);
    send_config(sockfd);
}
