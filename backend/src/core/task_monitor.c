// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "task_monitor.h"

#include "json_writer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "task_monitor";

typedef struct
{
    char name[configMAX_TASK_NAME_LEN];
    UBaseType_t task_number;
    TaskHandle_t handle;
    UBaseType_t first_hwm;
    UBaseType_t last_hwm;
    int64_t first_us;
    int64_t last_us;
    int misses;
    bool used;
} task_entry_t;

static task_entry_t s_table[TASKMON_MAX_TASKS];
static bool s_started = false;

static task_entry_t *find_entry(const char *name, UBaseType_t task_number)
{
    for (int i = 0; i < TASKMON_MAX_TASKS; i++)
    {
        if (s_table[i].used && s_table[i].task_number == task_number &&
            strncmp(s_table[i].name, name, sizeof(s_table[i].name)) == 0)
        {
            return &s_table[i];
        }
    }
    return NULL;
}

static task_entry_t *alloc_entry(void)
{
    for (int i = 0; i < TASKMON_MAX_TASKS; i++)
    {
        if (!s_table[i].used)
        {
            return &s_table[i];
        }
    }
    return NULL;
}

static void sample_once(void)
{
    // Margin over the live count: a task born between count and snapshot
    // must not overflow the array (the old code's heap race, fixed by
    // construction here with a static table + cap).
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    if (n > TASKMON_MAX_TASKS)
    {
        n = TASKMON_MAX_TASKS;
    }
    TaskStatus_t *snap = malloc(n * sizeof(TaskStatus_t));
    if (!snap)
    {
        return;
    }
    UBaseType_t count = uxTaskGetSystemState(snap, n, NULL);
    int64_t now = esp_timer_get_time();

    // Age out entries unseen this round (deleted tasks).
    for (int i = 0; i < TASKMON_MAX_TASKS; i++)
    {
        if (s_table[i].used)
        {
            s_table[i].misses++;
        }
    }

    for (UBaseType_t i = 0; i < count; i++)
    {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(snap[i].xHandle);
        task_entry_t *e = find_entry(snap[i].pcTaskName, snap[i].xTaskNumber);
        if (!e)
        {
            e = alloc_entry();
            if (!e)
            {
                continue;
            }
            memset(e, 0, sizeof(*e));
            strncpy(e->name, snap[i].pcTaskName, sizeof(e->name) - 1);
            e->task_number = snap[i].xTaskNumber;
            e->first_hwm = hwm;
            e->first_us = now;
            e->used = true;
        }
        else if (e->handle != NULL && e->handle != snap[i].xHandle)
        {
            // Handle recycled under a matching name+number: re-baseline,
            // the old floor belongs to a dead incarnation.
            e->first_hwm = hwm;
            e->first_us = now;
        }
        e->handle = snap[i].xHandle;
        e->last_hwm = hwm;
        e->last_us = now;
        e->misses = 0;
    }
    free(snap);

    for (int i = 0; i < TASKMON_MAX_TASKS; i++)
    {
        if (s_table[i].used && s_table[i].misses >= TASKMON_MISS_DROP)
        {
            s_table[i].used = false;
        }
    }
}

static void task_monitor_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        sample_once();
        vTaskDelay(pdMS_TO_TICKS(TASKMON_PERIOD_S * 1000));
    }
}

void task_monitor_start(void)
{
    if (s_started)
    {
        return;
    }
    s_started = true;
    // Baseline immediately so drift is measured from boot, not from the
    // first timer tick.
    sample_once();
    xTaskCreate(task_monitor_task, "task_monitor", 3072, NULL, 1, NULL);
    ESP_LOGI(TAG, "Stack sampler started (every %ds)", TASKMON_PERIOD_S);
}

static const char *status_for(UBaseType_t min_free)
{
    if (min_free < TASKMON_CRITICAL_WORDS)
    {
        return "critical";
    }
    if (min_free < TASKMON_WATCH_WORDS)
    {
        return "watch";
    }
    return "ok";
}

size_t task_monitor_json(char *buf, size_t len)
{
    json_writer_t w = {.buf = buf, .size = len, .off = 0};
    json_w_out(&w, "{\"type\":\"taskmon\",\"period_s\":%d,\"tasks\":[", TASKMON_PERIOD_S);
    bool first = true;
    for (int i = 0; i < TASKMON_MAX_TASKS; i++)
    {
        task_entry_t *e = &s_table[i];
        if (!e->used)
        {
            continue;
        }
        UBaseType_t drift = (e->first_hwm >= e->last_hwm) ? (e->first_hwm - e->last_hwm) : 0;
        int64_t elapsed_us = e->last_us - e->first_us;
        // Words per hour, integer math: drift * 3.6e9 us/h / elapsed.
        unsigned rate_wph = 0;
        if (elapsed_us > 0 && drift > 0)
        {
            rate_wph = (unsigned)((uint64_t)drift * 3600000000ULL / (uint64_t)elapsed_us);
        }
        json_w_out(&w,
                   "%s{\"name\":\"%s\",\"free_words\":%u,\"drift_words\":%u,"
                   "\"rate_wph\":%u,\"status\":\"%s\"}",
                   first ? "" : ",", e->name, (unsigned)e->last_hwm, (unsigned)drift,
                   rate_wph, status_for(e->last_hwm));
        first = false;
    }
    json_w_out(&w, "]}");
    if (buf && len > 0)
    {
        buf[w.off < len ? w.off : len - 1] = '\0';
    }
    return w.off;
}
