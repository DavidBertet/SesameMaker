// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <esp_http_server.h>
#include "cJSON.h"

typedef void (*wsserver_receive_callback)(const cJSON *root, int sockfd);

void start_websocket(httpd_handle_t server);

void on_ws_client_disconnected(int sockfd);

// Send message. Payloads are const: callers keep ownership, string
// literals pass without warnings on either side.
esp_err_t send_message_sockfd(const char *msg, int sockfd);
esp_err_t send_message_token(const char *msg, const char *token);
esp_err_t broadcast_message(const char *msg);

// True while a websocket with this fd is registered as a connected client.
// Used by the log bridge to drop subscribers whose socket vanished.
bool websocket_client_active(int sockfd);

// Listen to message received through callbacks

void register_callback(const char *type, wsserver_receive_callback callback);
void unregister_callback(const char *type, wsserver_receive_callback callback);
