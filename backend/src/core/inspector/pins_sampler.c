// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "pins_sampler.h"

#include "bus_capture.h"
#include "pin_inspector.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>

#define PIN_SAMPLER_MS 5
#define PIN_SAMPLER_STACK 2048
#define PIN_SAMPLER_PRIO 4

static int8_t s_last_level[GPIO_NUM_MAX];
static bool s_have_level[GPIO_NUM_MAX];
static bool s_saw_edge[GPIO_NUM_MAX];
static uint32_t s_edges[GPIO_NUM_MAX];
static uint32_t s_last_change_ms[GPIO_NUM_MAX];
static TaskHandle_t s_sampler_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void pin_sampler_task(void *arg)
{
    (void)arg;
    while (1)
    {
        uint32_t now = now_ms();
        // Deadline watchdog: stop a capture session nobody closes.
        bus_capture_tick(now);
        pin_provider_fn provider = pin_inspector_get_provider();
        if (provider)
        {
            pin_desc_t all[PIN_INSPECTOR_MAX];
            size_t count = provider(all, PIN_INSPECTOR_MAX);
            for (size_t i = 0; i < count; i++)
            {
                if (all[i].gpio < 0 || all[i].gpio >= GPIO_NUM_MAX ||
                    !all[i].name || all[i].type != PIN_TYPE_DIGITAL)
                {
                    continue;
                }
                int gpio = all[i].gpio;
                int lvl = gpio_get_level((gpio_num_t)gpio) ? 1 : 0;
                if (s_have_level[gpio] && lvl != s_last_level[gpio])
                {
                    s_edges[gpio]++;
                    s_last_change_ms[gpio] = now;
                    s_saw_edge[gpio] = true;
                }
                s_last_level[gpio] = (int8_t)lvl;
                s_have_level[gpio] = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PIN_SAMPLER_MS));
    }
}

void pins_sampler_start(void)
{
    if (!s_sampler_task)
    {
        xTaskCreate(pin_sampler_task, "pin_sampler", PIN_SAMPLER_STACK, NULL,
                    PIN_SAMPLER_PRIO, &s_sampler_task);
    }
}

void pins_sampler_snapshot(int gpio, uint32_t now_ms, uint32_t *edges_out,
                           bool *has_activity_out, uint32_t *idle_ms_out)
{
    if (gpio < 0 || gpio >= GPIO_NUM_MAX)
    {
        return;
    }
    if (edges_out)
    {
        *edges_out = s_edges[gpio];
    }
    if (has_activity_out)
    {
        *has_activity_out = s_saw_edge[gpio];
    }
    if (idle_ms_out && s_saw_edge[gpio])
    {
        // Signed subtraction: immune to the 49-day uint32 wrap.
        int32_t idle = (int32_t)(now_ms - s_last_change_ms[gpio]);
        *idle_ms_out = (uint32_t)(idle < 0 ? 0 : idle);
    }
}
