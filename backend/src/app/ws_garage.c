// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_garage.h"

#include "garage_controller.h"
#include "garage_uart.h"
#include "mqtt.h"
#include "raw_json.h"

#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WS_GARAGE";

static void send_error(const char *message)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"type\":\"error\",\"message\":\"%s\"}", message);
    broadcast_message(json);
}

void ws_handle_get_garage_status(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "get_garage_status");
    char json[512];
    garage_controller_get_status_json(json, sizeof(json));
    send_message_sockfd(json, sockfd);
}

static const char *get_action(const cJSON *root)
{
    cJSON *node = cJSON_GetObjectItem(root, "action");
    return cJSON_IsString(node) ? node->valuestring : NULL;
}

void ws_handle_garage_door_command(const cJSON *root, int sockfd)
{
    (void)sockfd;
    const char *action = get_action(root);
    ESP_LOGI(TAG, "door_command: %s", action ? action : "(null)");
    esp_err_t ret = action ? garage_controller_door_action(action) : ESP_ERR_INVALID_ARG;
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        send_error("Door control not supported by the active protocol yet");
    }
    else if (ret != ESP_OK)
    {
        send_error("Invalid door action (toggle|open|close|stop)");
    }
}

void ws_handle_garage_light_command(const cJSON *root, int sockfd)
{
    (void)sockfd;
    const char *action = get_action(root);
    ESP_LOGI(TAG, "light_command: %s", action ? action : "(null)");
    esp_err_t ret = action ? garage_controller_light_action(action) : ESP_ERR_INVALID_ARG;
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        send_error("Light control not supported by the active protocol");
    }
    else if (ret != ESP_OK)
    {
        send_error("Invalid light action (toggle|on|off)");
    }
}

void ws_handle_garage_lock_command(const cJSON *root, int sockfd)
{
    (void)sockfd;
    const char *action = get_action(root);
    ESP_LOGI(TAG, "lock_command: %s", action ? action : "(null)");
    esp_err_t ret = action ? garage_controller_lock_action(action) : ESP_ERR_INVALID_ARG;
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        send_error("Lock control not supported by the active protocol");
    }
    else if (ret != ESP_OK)
    {
        send_error("Invalid lock action (toggle|lock|unlock)");
    }
}

void ws_handle_get_garage_raw(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "get_garage_raw");
    // This runs on the httpd task (default 4KB stack); the raw dump can be
    // ~2 KB, so build it on the heap instead of a stack buffer.
    const raw_json_event_t *rx =
        (const raw_json_event_t *)garage_uart_rx_log();
    const raw_json_event_t *tx =
        (const raw_json_event_t *)garage_uart_tx_log();
    size_t need = raw_json_garage_payload(
        NULL, 0, rx, GARAGE_UART_RX_LOG_SIZE, garage_uart_rx_byte_count(),
        garage_uart_rx_byte_count(),
        tx, GARAGE_UART_TX_LOG_SIZE, garage_uart_tx_byte_count(),
        garage_uart_tx_byte_count());
    char *json = malloc(need + 1);
    if (!json)
    {
        send_error("Out of memory building raw dump");
        return;
    }
    raw_json_garage_payload(json, need + 1, rx, GARAGE_UART_RX_LOG_SIZE,
                            garage_uart_rx_byte_count(),
                            garage_uart_rx_byte_count(),
                            tx, GARAGE_UART_TX_LOG_SIZE,
                            garage_uart_tx_byte_count(),
                            garage_uart_tx_byte_count());
    send_message_sockfd(json, sockfd);
    free(json);
}

void ws_handle_garage_sync(const cJSON *root, int sockfd)
{
    (void)sockfd;
    ESP_LOGI(TAG, "garage_sync");
    garage_controller_sync();
}

void ws_handle_get_mqtt_config(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "get_mqtt_config");

    mqtt_config_t cfg;
    if (mqtt_get_config(&cfg) != ESP_OK)
    {
        send_error("Failed to read MQTT config");
        return;
    }

    char json[512];
    const char *mode = (cfg.mode == MQTT_MODE_HA)     ? "ha"
                       : (cfg.mode == MQTT_MODE_MASTER) ? "master"
                                                        : "off";
    snprintf(json, sizeof(json),
             "{\"type\":\"mqtt_config\",\"mode\":\"%s\",\"uri\":\"%.127s\","
             "\"username\":\"%.63s\",\"password_set\":%s,\"topic_prefix\":\"%.63s\"}",
             mode, cfg.uri, cfg.username,
             strlen(cfg.password) > 0 ? "true" : "false", cfg.topic_prefix);
    send_message_sockfd(json, sockfd);
}

void ws_handle_set_mqtt_config(const cJSON *root, int sockfd)
{
    ESP_LOGI(TAG, "set_mqtt_config");

    mqtt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    cJSON *uri = cJSON_GetObjectItem(root, "uri");
    cJSON *username = cJSON_GetObjectItem(root, "username");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *topic_prefix = cJSON_GetObjectItem(root, "topic_prefix");

    cfg.mode = MQTT_MODE_OFF;
    if (mode && cJSON_IsString(mode))
    {
        if (strcmp(mode->valuestring, "master") == 0)
            cfg.mode = MQTT_MODE_MASTER;
        else if (strcmp(mode->valuestring, "ha") == 0)
            cfg.mode = MQTT_MODE_HA;
    }
    if (uri && cJSON_IsString(uri))
        snprintf(cfg.uri, sizeof(cfg.uri), "%s", uri->valuestring);
    if (username && cJSON_IsString(username))
        snprintf(cfg.username, sizeof(cfg.username), "%s", username->valuestring);
    if (password && cJSON_IsString(password))
        snprintf(cfg.password, sizeof(cfg.password), "%s", password->valuestring);
    if (topic_prefix && cJSON_IsString(topic_prefix) && strlen(topic_prefix->valuestring) > 0)
        snprintf(cfg.topic_prefix, sizeof(cfg.topic_prefix), "%s", topic_prefix->valuestring);
    else
        snprintf(cfg.topic_prefix, sizeof(cfg.topic_prefix), "home/sesame");

    esp_err_t ret = mqtt_set_config(&cfg);
    char json[128];
    if (ret == ESP_OK)
        snprintf(json, sizeof(json), "{\"type\":\"mqtt_config_saved\",\"success\":true}");
    else
        snprintf(json, sizeof(json),
                 "{\"type\":\"mqtt_config_saved\",\"success\":false,\"message\":\"Invalid config\"}");
    send_message_sockfd(json, sockfd);
}
