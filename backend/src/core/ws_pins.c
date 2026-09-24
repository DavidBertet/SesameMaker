// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Generic live-pin + bus endpoint ("get_gpio_state" -> "gpio_state").
// Sampling is the only IDF-dependent part:
//   digital pins: level via gpio_get_level + edge counters (5 ms task).
//   analog pins: ADC oneshot with line-fitting calibration, on request.
//   buses: on-demand GPIO-ISR edge capture + framing analysis (UART/I2C/SPI).
// WHAT is watched comes from the app providers (pin_inspector_set_provider /
// set_bus_provider) so this file never changes per project.
//
// Capture is on-demand only: the UI sends bus_capture_start while its tab is
// open (plus a heartbeat) and bus_capture_stop on close. A 60 s deadline
// stops a session nobody closes. Continuous ISR capture at bus rates would
// be wasteful, and polled sampling could never validate framing anyway.

#include "ws_pins.h"

#include "pin_inspector.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WS_PINS";

// ---- Activity sampler: 5 ms poll loop counting edges on DIGITAL pins only.
// Bus lines are ISR-captured while monitored (below); analog is on-request.
#define PIN_SAMPLER_MS 5
#define PIN_SAMPLER_STACK 2048
#define PIN_SAMPLER_PRIO 4

static int8_t s_last_level[GPIO_NUM_MAX];
static bool s_have_level[GPIO_NUM_MAX];
static bool s_saw_edge[GPIO_NUM_MAX];
static uint32_t s_edges[GPIO_NUM_MAX];
static uint32_t s_last_change_ms[GPIO_NUM_MAX];
static TaskHandle_t s_sampler_task;

// Capture session state (defined below); forward declarations for the
// sampler task, which doubles as the session watchdog.
static bool s_capture_active;
static uint32_t s_capture_deadline_ms;
static void capture_stop(void);

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
        if (s_capture_active && (int32_t)(now - s_capture_deadline_ms) > 0)
        {
            ESP_LOGI(TAG, "capture expired");
            capture_stop();
        }
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

// ---- Bus capture: ISR timestamps into a ring, framing analysis on request.

#define BUS_CAP_LEN 2048
#define BUS_CAPTURE_TIMEOUT_MS 60000

typedef struct
{
    uint32_t t_us;
    uint8_t gpio;
    uint8_t level; // level AFTER the transition
} cap_edge_t;

static cap_edge_t s_cap[BUS_CAP_LEN];
static volatile uint32_t s_cap_head; // ISR writer
static uint32_t s_cap_tail;          // handler-task reader
static bool s_isr_service;
static bool s_installed[GPIO_NUM_MAX]; // ISRs installed by us
static bus_run_t s_runs[BUS_INSPECTOR_MAX];
static size_t s_nruns;

static void IRAM_ATTR cap_isr(void *arg)
{
    int gpio = (int)(intptr_t)arg;
    uint32_t h = s_cap_head;
    s_cap[h % BUS_CAP_LEN].t_us = (uint32_t)esp_timer_get_time();
    s_cap[h % BUS_CAP_LEN].gpio = (uint8_t)gpio;
    s_cap[h % BUS_CAP_LEN].level = gpio_get_level((gpio_num_t)gpio) ? 1 : 0;
    s_cap_head = h + 1;
}

// Collect the watched gpios of usable bus entries (deduped). Returns count.
static size_t bus_gpios(const bus_desc_t *b, size_t nb, int8_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < nb; i++)
    {
        int8_t pins[2];
        size_t np = 0;
        switch (b[i].type)
        {
        case BUS_UART:
            pins[0] = b[i].rx_gpio;
            np = 1;
            break;
        case BUS_I2C:
            pins[0] = b[i].sda_gpio;
            pins[1] = b[i].scl_gpio;
            np = 2;
            break;
        case BUS_SPI:
            pins[0] = b[i].sck_gpio;
            pins[1] = b[i].cs_gpio;
            np = 2;
            break;
        }
        for (size_t k = 0; k < np; k++)
        {
            if (pins[k] < 0 || pins[k] >= GPIO_NUM_MAX)
            {
                continue;
            }
            bool dup = false;
            for (size_t j = 0; j < n; j++)
            {
                if (out[j] == pins[k])
                {
                    dup = true;
                    break;
                }
            }
            if (!dup && n < max)
            {
                out[n++] = pins[k];
            }
        }
    }
    return n;
}

static bool bus_usable(const bus_desc_t *d)
{
    if (!d->name)
    {
        return false;
    }
    switch (d->type)
    {
    case BUS_UART:
        return d->rx_gpio >= 0 && d->baud > 0;
    case BUS_I2C:
        return d->sda_gpio >= 0 && d->scl_gpio >= 0 &&
               d->sda_gpio != d->scl_gpio;
    case BUS_SPI:
        return d->sck_gpio >= 0 && d->cs_gpio >= 0 &&
               d->sck_gpio != d->cs_gpio;
    }
    return false;
}

// Compact the bus provider table (drop unusable slots).
static size_t bus_table(bus_desc_t *out, size_t max)
{
    size_t n = 0;
    bus_provider_fn bp = pin_inspector_get_bus_provider();
    if (!bp)
    {
        return 0;
    }
    bus_desc_t all[BUS_INSPECTOR_MAX];
    size_t count = bp(all, BUS_INSPECTOR_MAX);
    for (size_t i = 0; i < count && n < max; i++)
    {
        if (bus_usable(&all[i]))
        {
            out[n++] = all[i];
        }
    }
    return n;
}

// Must run on a task (not in ISR).
static void capture_stop(void)
{
    for (int g = 0; g < GPIO_NUM_MAX; g++)
    {
        if (s_installed[g])
        {
            gpio_isr_handler_remove((gpio_num_t)g);
            s_installed[g] = false;
        }
    }
    s_capture_active = false;
}

static void capture_start(void)
{
    bus_desc_t buses[BUS_INSPECTOR_MAX];
    size_t nb = bus_table(buses, BUS_INSPECTOR_MAX);
    int8_t gpios[2 * BUS_INSPECTOR_MAX];
    size_t ng = bus_gpios(buses, nb, gpios, sizeof(gpios) / sizeof(gpios[0]));
    if (ng == 0)
    {
        ESP_LOGW(TAG, "capture start: no bus lines declared");
        return;
    }
    if (!s_isr_service)
    {
        if (gpio_install_isr_service(ESP_INTR_FLAG_IRAM) != ESP_OK)
        {
            ESP_LOGW(TAG, "capture start: no ISR service");
            return;
        }
        s_isr_service = true;
    }
    bool fresh = !s_capture_active;
    // Drop ISRs for pins that left the table; keep the rest installed so a
    // heartbeat refresh costs no re-arm gap.
    for (int g = 0; g < GPIO_NUM_MAX; g++)
    {
        if (!s_installed[g])
        {
            continue;
        }
        bool wanted = false;
        for (size_t k = 0; k < ng; k++)
        {
            if (gpios[k] == g)
            {
                wanted = true;
                break;
            }
        }
        if (!wanted)
        {
            gpio_isr_handler_remove((gpio_num_t)g);
            s_installed[g] = false;
        }
    }
    for (size_t k = 0; k < ng; k++)
    {
        if (s_installed[gpios[k]])
        {
            continue;
        }
        gpio_set_intr_type((gpio_num_t)gpios[k], GPIO_INTR_ANYEDGE);
        if (gpio_isr_handler_add((gpio_num_t)gpios[k], cap_isr,
                                 (void *)(intptr_t)gpios[k]) != ESP_OK)
        {
            // Owned by another driver: leave it alone, skip the pin.
            ESP_LOGW(TAG, "capture: GPIO%d ISR busy, skipped", gpios[k]);
            continue;
        }
        s_installed[gpios[k]] = true;
    }
    if (fresh)
    {
        s_cap_head = 0;
        s_cap_tail = 0;
        memset(s_runs, 0, sizeof(s_runs));
        s_nruns = 0;
    }
    s_capture_active = true;
    s_capture_deadline_ms = now_ms() + BUS_CAPTURE_TIMEOUT_MS;
    ESP_LOGI(TAG, "capture start: %u bus lines", (unsigned)ng);
}

void ws_handle_bus_capture_start(const cJSON *root, int sockfd)
{
    (void)root;
    capture_start();
    char json[64];
    snprintf(json, sizeof(json), "{\"type\":\"bus_capture\",\"active\":%s}",
             s_capture_active ? "true" : "false");
    send_message_sockfd(json, sockfd);
}

void ws_handle_bus_capture_stop(const cJSON *root, int sockfd)
{
    (void)root;
    capture_stop();
    char json[64];
    snprintf(json, sizeof(json), "{\"type\":\"bus_capture\",\"active\":false}");
    send_message_sockfd(json, sockfd);
}

// ---- ADC: lazy per-unit init, channels configured on first use, one
// curve-fitting calibration handle per (unit, channel) slot (recreated if a
// channel asks for a different attenuation than the handle was built with).
#define ADC_MAX_CH 10

typedef struct
{
    adc_cali_handle_t handle;
    int atten;
} adc_cali_slot_t;

static adc_oneshot_unit_handle_t s_adc_unit[2];
static adc_cali_slot_t s_adc_cali[2][ADC_MAX_CH];
static uint32_t s_adc_ch_mask[2];

static bool analog_read(int8_t gpio, int atten_cfg, int *raw_out, int *mv_out)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel((int)gpio, &unit, &channel) != ESP_OK)
    {
        return false;
    }
    if (unit < 0 || unit > 1)
    {
        return false;
    }
    adc_atten_t atten = (atten_cfg >= ADC_ATTEN_DB_0 && atten_cfg <= ADC_ATTEN_DB_12)
                            ? (adc_atten_t)atten_cfg
                            : ADC_ATTEN_DB_12;
    if (!s_adc_unit[unit])
    {
        adc_oneshot_unit_init_cfg_t ucfg = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        if (adc_oneshot_new_unit(&ucfg, &s_adc_unit[unit]) != ESP_OK)
        {
            return false;
        }
    }
    if (!(s_adc_ch_mask[unit] & (1u << (unsigned)channel)))
    {
        adc_oneshot_chan_cfg_t ccfg = {
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_oneshot_config_channel(s_adc_unit[unit], channel, &ccfg) != ESP_OK)
        {
            return false;
        }
        s_adc_ch_mask[unit] |= (1u << (unsigned)channel);
    }
    if ((unsigned)channel >= ADC_MAX_CH)
    {
        return false;
    }
    adc_cali_slot_t *slot = &s_adc_cali[unit][channel];
    if (!slot->handle || slot->atten != (int)atten)
    {
        if (slot->handle)
        {
            adc_cali_delete_scheme_curve_fitting(slot->handle);
            slot->handle = NULL;
        }
        adc_cali_curve_fitting_config_t lcfg = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&lcfg, &slot->handle) != ESP_OK)
        {
            slot->handle = NULL;
            return false;
        }
        slot->atten = (int)atten;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc_unit[unit], channel, &raw) != ESP_OK)
    {
        return false;
    }
    int mv = 0;
    if (adc_cali_raw_to_voltage(slot->handle, raw, &mv) != ESP_OK)
    {
        return false;
    }
    *raw_out = raw;
    *mv_out = mv;
    return true;
}

static void sample_one(const pin_desc_t *d, pin_sample_t *s)
{
    memset(s, 0, sizeof(*s));
    if (d->type == PIN_TYPE_ANALOG)
    {
        int raw = 0, mv = 0;
        if (analog_read(d->gpio, d->adc_atten, &raw, &mv))
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
}

// Analyze one bus over the captured window, folding deltas into its runtime.
static void analyze_bus(const bus_desc_t *d, bus_run_t *run,
                        const bus_edge_t *ev, size_t nev, uint32_t now)
{
    switch (d->type)
    {
    case BUS_UART: {
        bus_uart_cfg_t cfg = {
            .gpio = d->rx_gpio,
            .baud = d->baud,
            .data_bits = d->data_bits ? d->data_bits : 8,
            .parity = d->parity,
            .stop_bits = d->stop_bits ? d->stop_bits : 1,
        };
        bus_uart_result_t r;
        bus_uart_analyze(ev, nev, &cfg, &r);
        bus_run_add_uart(run, &r, now);
        break;
    }
    case BUS_I2C: {
        bus_i2c_result_t r;
        bus_i2c_analyze(ev, nev, d->sda_gpio, d->scl_gpio, &r);
        bus_run_add_i2c(run, &r, now);
        break;
    }
    case BUS_SPI: {
        bus_spi_result_t r;
        bus_spi_analyze(ev, nev, d->sck_gpio, d->cs_gpio, &r);
        bus_run_add_spi(run, &r, now);
        break;
    }
    }
}

void ws_handle_get_gpio_state(const cJSON *root, int sockfd)
{
    (void)root;
    ESP_LOGI(TAG, "get_gpio_state");

    // Start the activity sampler on first request (providers are registered
    // at boot, before any request can arrive).
    if (!s_sampler_task)
    {
        xTaskCreate(pin_sampler_task, "pin_sampler", PIN_SAMPLER_STACK, NULL,
                    PIN_SAMPLER_PRIO, &s_sampler_task);
    }

    // Compact the provider table (drop unusable slots), sampling each pin.
    // Edge counters stay digital-only: bus lines are ISR-captured instead.
    pin_desc_t descs[PIN_INSPECTOR_MAX];
    pin_sample_t samples[PIN_INSPECTOR_MAX];
    size_t n = 0;
    uint32_t now = now_ms();
    pin_provider_fn provider = pin_inspector_get_provider();
    if (provider)
    {
        pin_desc_t all[PIN_INSPECTOR_MAX];
        size_t count = provider(all, PIN_INSPECTOR_MAX);
        for (size_t i = 0; i < count && n < PIN_INSPECTOR_MAX; i++)
        {
            if (all[i].gpio < 0 || all[i].gpio >= GPIO_NUM_MAX || !all[i].name)
            {
                continue;
            }
            descs[n] = all[i];
            sample_one(&descs[n], &samples[n]);
            if (descs[n].type == PIN_TYPE_DIGITAL)
            {
                int gpio = descs[n].gpio;
                samples[n].edges = s_edges[gpio];
                samples[n].has_activity = s_saw_edge[gpio];
                if (s_saw_edge[gpio])
                {
                    // Signed subtraction: immune to the 49-day uint32 wrap.
                    int32_t idle = (int32_t)(now - s_last_change_ms[gpio]);
                    samples[n].idle_ms = (uint32_t)(idle < 0 ? 0 : idle);
                }
            }
            n++;
        }
    }

    // Bus framing analysis over the edges captured since the last request.
    // Runtime totals are indexed by bus-table position, so providers must
    // return entries in stable order; a changed table restarts the totals.
    bus_desc_t bdescs[BUS_INSPECTOR_MAX];
    size_t nb = 0;
    bool with_buses = false;
    if (s_capture_active)
    {
        nb = bus_table(bdescs, BUS_INSPECTOR_MAX);
        if (nb != s_nruns)
        {
            memset(s_runs, 0, sizeof(s_runs));
            s_nruns = nb;
        }
        uint32_t head = s_cap_head;
        if (head - s_cap_tail > BUS_CAP_LEN)
        {
            s_cap_tail = head - BUS_CAP_LEN; // overflow: drop oldest
        }
        size_t nev = (size_t)(head - s_cap_tail);
        bus_edge_t *ev = NULL;
        if (nev > 0)
        {
            ev = malloc(nev * sizeof(*ev));
        }
        if (nev == 0 || ev)
        {
            for (size_t i = 0; i < nev; i++)
            {
                cap_edge_t *c = &s_cap[(s_cap_tail + i) % BUS_CAP_LEN];
                ev[i].gpio = c->gpio;
                ev[i].level = c->level;
                ev[i].t_us = c->t_us;
            }
            for (size_t b = 0; b < nb; b++)
            {
                analyze_bus(&bdescs[b], &s_runs[b], ev, nev, now);
            }
            free(ev);
            s_cap_tail = head;
        }
        with_buses = nb > 0;
    }

    // Runs on the httpd task (default 4KB stack): build the payload on the
    // heap like the garage_raw dump does.
    size_t need = gpio_state_format(NULL, 0, descs, samples, n,
                                    with_buses ? bdescs : NULL,
                                    with_buses ? s_runs : NULL,
                                    with_buses ? nb : 0, now);
    char *json = malloc(need + 1);
    if (!json)
    {
        char err[160];
        snprintf(err, sizeof(err),
                 "{\"type\":\"error\",\"message\":\"Out of memory reading pins\"}");
        broadcast_message(err);
        return;
    }
    gpio_state_format(json, need + 1, descs, samples, n,
                      with_buses ? bdescs : NULL, with_buses ? s_runs : NULL,
                      with_buses ? nb : 0, now);
    send_message_sockfd(json, sockfd);
    free(json);
}
