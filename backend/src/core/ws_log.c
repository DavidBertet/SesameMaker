// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ws_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "websocket.h"

static const char *TAG = "ws_log";

#define LOG_QUEUE_SIZE 64
#define LOG_MSG_MAX_LEN 512
#define MAX_LOG_SUBSCRIBERS 4

static QueueHandle_t log_queue = NULL;
static TaskHandle_t log_task_handle = NULL;
static int log_subscribers[MAX_LOG_SUBSCRIBERS];
static int subscriber_count = 0;

// Reentrancy guard: while the log task is inside the WebSocket send path
// (broadcast_message -> send_message_sockfd -> ESP_LOGI), those log calls
// would otherwise feed straight back into this handler and loop forever.
static volatile bool in_log_send = false;

typedef struct
{
  char msg[LOG_MSG_MAX_LEN];
} log_entry_t;

// Strip everything a browser WebSocket cannot handle: ANSI color escapes
// (\x1b[...m) and any control / non-ASCII byte. Raw device log lines can
// carry binary junk or a UTF-8 character cut mid-sequence at the truncation
// boundary, which makes the browser fail with "could not decode a text frame
// as UTF-8".
static void sanitize_log(const char *in, char *out, size_t out_sz)
{
  size_t o = 0;
  for (size_t i = 0; in[i] && o + 1 < out_sz; i++)
  {
    uint8_t c = (uint8_t)in[i];
    if (c == 0x1B)
    {
      // ANSI escape: skip the CSI sequence up to its terminating 'm'.
      while (in[i] && in[i] != 'm')
      {
        i++;
      }
      continue;
    }
    if (c < 0x20 || c >= 0x7F)
    {
      continue;
    }
    if ((c == '"' || c == '\\') && o + 2 < out_sz)
    {
      // Keep the JSON envelope parseable.
      out[o++] = '\\';
      out[o++] = (char)c;
      continue;
    }
    out[o++] = (char)c;
  }
  out[o] = '\0';
}

static int vprintf_log_level(const char *fmt, va_list args)
{
  if (subscriber_count > 0 && log_queue && !in_log_send)
  {
    log_entry_t entry;
    int len = vsnprintf(entry.msg, sizeof(entry.msg), fmt, args);
    if (len > 0)
    {
      xQueueSend(log_queue, &entry, 0);
    }
    return len;
  }

  return vprintf(fmt, args);
}

static void log_listener_task(void *arg)
{
  log_entry_t entry;

  while (1)
  {
    if (xQueueReceive(log_queue, &entry, portMAX_DELAY) == pdTRUE)
    {
      // Self-heal: drop subscribers whose socket is no longer a live client
      // (page refreshed, console tab closed, socket purged by httpd, ...).
      // A vanished client never sent log_stop, but it must not keep the
      // bridge engaged and forwarding logs forever. Once pruned to zero the
      // vprintf hook falls straight through to the normal console output.
      int i = 0;
      while (i < subscriber_count)
      {
        if (websocket_client_active(log_subscribers[i]))
        {
          i++;
        }
        else
        {
          log_subscribers[i] = log_subscribers[--subscriber_count];
        }
      }

      if (subscriber_count > 0)
      {
        char clean[LOG_MSG_MAX_LEN];
        sanitize_log(entry.msg, clean, sizeof(clean));

        char *json = malloc(LOG_MSG_MAX_LEN + 128);
        if (json)
        {
          snprintf(json, LOG_MSG_MAX_LEN + 128,
                   "{\"type\":\"log\",\"message\":\"%.500s\"}",
                   clean);
          in_log_send = true;
          broadcast_message(json);
          in_log_send = false;
          free(json);
        }
      }
    }
  }
}

void ws_handle_log_start(const cJSON *root, int sockfd)
{
  ESP_LOGI(TAG, "Log streaming started for client %d", sockfd);

  bool already = false;
  for (int i = 0; i < subscriber_count; i++)
  {
    if (log_subscribers[i] == sockfd)
    {
      already = true;
      break;
    }
  }

  if (!already && subscriber_count < MAX_LOG_SUBSCRIBERS)
  {
    log_subscribers[subscriber_count++] = sockfd;
  }

  if (subscriber_count == 1)
  {
    esp_log_set_vprintf(vprintf_log_level);
  }

  char resp[] = "{\"type\":\"log_started\"}";
  send_message_sockfd(resp, sockfd);
}

void ws_handle_log_stop(const cJSON *root, int sockfd)
{
  ESP_LOGI(TAG, "Log streaming stopped for client %d", sockfd);

  for (int i = 0; i < subscriber_count; i++)
  {
    if (log_subscribers[i] == sockfd)
    {
      log_subscribers[i] = log_subscribers[--subscriber_count];
      break;
    }
  }

  if (subscriber_count == 0)
  {
    esp_log_set_vprintf(vprintf);
  }

  char resp[] = "{\"type\":\"log_stopped\"}";
  send_message_sockfd(resp, sockfd);
}

void ws_log_client_disconnected(int sockfd)
{
  bool removed = false;
  for (int i = 0; i < subscriber_count; i++)
  {
    if (log_subscribers[i] == sockfd)
    {
      log_subscribers[i] = log_subscribers[--subscriber_count];
      removed = true;
      break;
    }
  }
  if (removed && subscriber_count == 0)
  {
    // Last subscriber gone: release the log bridge, resume normal console.
    esp_log_set_vprintf(vprintf);
  }
}

void ws_log_init(void)
{
  log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(log_entry_t));

  xTaskCreate(log_listener_task, "ws_log_task", 4096, NULL, 5, &log_task_handle);

  ESP_LOGI(TAG, "Log forwarding initialized");
}

void ws_log_deinit(void)
{
  esp_log_set_vprintf(vprintf);

  if (log_task_handle)
  {
    vTaskDelete(log_task_handle);
    log_task_handle = NULL;
  }

  if (log_queue)
  {
    vQueueDelete(log_queue);
    log_queue = NULL;
  }

  subscriber_count = 0;
}
