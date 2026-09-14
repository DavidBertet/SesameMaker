// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// MQTT bridge settings endpoints (core).

#include "ws_mqtt.h"

#include "mqtt.h"
#include "websocket.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "WS_MQTT";

void ws_handle_get_mqtt_config(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_mqtt_config");

    mqtt_config_t cfg;
    if (mqtt_get_config(&cfg) != ESP_OK)
    {
        char json[160];
        snprintf(json, sizeof(json), "{\"type\":\"error\",\"message\":\"%s\"}",
                 "Failed to read MQTT config");
        broadcast_message(json);
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
