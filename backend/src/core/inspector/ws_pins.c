// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Live-pin + bus websocket endpoint ("get_gpio_state" -> "gpio_state").
//
// Thin orchestration only; the work lives in focused modules:
//   pins_sampler.h  background 5 ms edge counters (digital pins)
//   pins_adc.h      lazy ADC oneshot reads (analog pins)
//   bus_capture.h   on-demand ISR edge capture + framing analysis (buses)
//   pin_inspector.h WHAT is watched (app providers) + payload formatting
// WHAT is watched comes from the app providers (pin_inspector_set_provider /
// set_bus_provider) so this file never changes per project.

#include "ws_pins.h"

#include "bus_capture.h"
#include "pin_inspector.h"
#include "pins_adc.h"
#include "pins_sampler.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WS_PINS";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sample_one(const pin_desc_t *d, pin_sample_t *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    if (d->type == PIN_TYPE_ANALOG)
    {
        int raw = 0, mv = 0;
        if (pins_adc_read(d->gpio, d->adc_atten, &raw, &mv))
        {
            s->has_analog = true;
            s->raw = raw;
            s->mv = mv;
        }
        return;
    }
    int level = gpio_get_level((gpio_num_t)d->gpio);
    s->level = (level != 0) ? 1 : 0;
    if (d->type == PIN_TYPE_DIGITAL && !d->is_output && d->report_hit)
    {
        s->has_hit = true;
        s->hit = pin_hit_of(s->level, d->active_low);
    }
    if (d->type == PIN_TYPE_DIGITAL)
    {
        // Edge counters stay digital-only: bus lines are ISR-captured instead.
        uint32_t idle = 0;
        pins_sampler_snapshot(d->gpio, now, &s->edges, &s->has_activity, &idle);
        if (s->has_activity)
        {
            s->idle_ms = idle;
        }
    }
}

// Compact the provider table (drop unusable slots), sampling each pin.
static size_t sample_pins(pin_desc_t *descs, pin_sample_t *samples, uint32_t now)
{
    size_t n = 0;
    pin_provider_fn provider = pin_inspector_get_provider();
    if (!provider)
    {
        return 0;
    }
    pin_desc_t all[PIN_INSPECTOR_MAX];
    size_t count = provider(all, PIN_INSPECTOR_MAX);
    for (size_t i = 0; i < count && n < PIN_INSPECTOR_MAX; i++)
    {
        if (all[i].gpio < 0 || all[i].gpio >= GPIO_NUM_MAX || !all[i].name)
        {
            continue;
        }
        descs[n] = all[i];
        sample_one(&descs[n], &samples[n], now);
        n++;
    }
    return n;
}

// Runs on the httpd task (default 4KB stack): build the payload on the
// heap like the garage_raw dump does.
static void send_gpio_state(const pin_desc_t *descs, const pin_sample_t *samples,
                            size_t n, const bus_desc_t *bdescs,
                            const bus_run_t *runs, size_t nb, uint32_t now,
                            int sockfd)
{
    size_t need = gpio_state_format(NULL, 0, descs, samples, n, bdescs, runs, nb, now);
    char *json = malloc(need + 1);
    if (!json)
    {
        char err[160];
        snprintf(err, sizeof(err),
                 "{\"type\":\"error\",\"message\":\"Out of memory reading pins\"}");
        broadcast_message(err);
        return;
    }
    gpio_state_format(json, need + 1, descs, samples, n, bdescs, runs, nb, now);
    send_message_sockfd(json, sockfd);
    free(json);
}

void ws_handle_get_gpio_state(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_gpio_state");

    // Start the activity sampler on first request (providers are registered
    // at boot, before any request can arrive).
    pins_sampler_start();

    uint32_t now = now_ms();
    pin_desc_t descs[PIN_INSPECTOR_MAX];
    pin_sample_t samples[PIN_INSPECTOR_MAX];
    size_t n = sample_pins(descs, samples, now);

    // Bus framing analysis over the edges captured since the last request.
    bus_desc_t bdescs[BUS_INSPECTOR_MAX];
    size_t nb = 0;
    const bus_run_t *runs = NULL;
    bool with_buses = false;
    if (bus_capture_active())
    {
        nb = bus_capture_table(bdescs, BUS_INSPECTOR_MAX);
        bus_capture_poll(bdescs, nb, now);
        runs = bus_capture_runs();
        with_buses = nb > 0;
    }

    send_gpio_state(descs, samples, n,
                    with_buses ? bdescs : NULL, with_buses ? runs : NULL,
                    with_buses ? nb : 0, now, sockfd);
}

void ws_handle_bus_capture_start(const cJSON *root, int sockfd)
{
    (void)root;
    bus_capture_start();
    char json[64];
    snprintf(json, sizeof(json), "{\"type\":\"bus_capture\",\"active\":%s}",
             bus_capture_active() ? "true" : "false");
    send_message_sockfd(json, sockfd);
}

void ws_handle_bus_capture_stop(const cJSON *root, int sockfd)
{
    (void)root;
    bus_capture_stop();
    char json[64];
    snprintf(json, sizeof(json), "{\"type\":\"bus_capture\",\"active\":false}");
    send_message_sockfd(json, sockfd);
}
