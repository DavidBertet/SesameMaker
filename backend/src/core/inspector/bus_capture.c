// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "bus_capture.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

// TAG kept as WS_PINS so existing `--tag WS_PINS` log filters keep working.
static const char *TAG = "WS_PINS";

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
static bool s_capture_active;
static uint32_t s_capture_deadline_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void IRAM_ATTR cap_isr(void *arg)
{
    int gpio = (int)(intptr_t)arg;
    uint32_t h = s_cap_head;
    s_cap[h % BUS_CAP_LEN].t_us = (uint32_t)esp_timer_get_time();
    s_cap[h % BUS_CAP_LEN].gpio = (uint8_t)gpio;
    s_cap[h % BUS_CAP_LEN].level = gpio_get_level((gpio_num_t)gpio) ? 1 : 0;
    s_cap_head = h + 1;
}

// Compact the bus provider table (drop unusable slots).
size_t bus_capture_table(bus_desc_t *out, size_t max)
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
        if (bus_desc_usable(&all[i]))
        {
            out[n++] = all[i];
        }
    }
    return n;
}

// Must run on a task (not in ISR).
void bus_capture_stop(void)
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

void bus_capture_start(void)
{
    bus_desc_t buses[BUS_INSPECTOR_MAX];
    size_t nb = bus_capture_table(buses, BUS_INSPECTOR_MAX);
    int8_t gpios[2 * BUS_INSPECTOR_MAX];
    size_t ng = bus_desc_gpios(buses, nb, gpios, sizeof(gpios) / sizeof(gpios[0]));
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
        if (gpios[k] < 0 || gpios[k] >= GPIO_NUM_MAX)
        {
            continue;
        }
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

bool bus_capture_active(void)
{
    return s_capture_active;
}

void bus_capture_tick(uint32_t now)
{
    if (s_capture_active && (int32_t)(now - s_capture_deadline_ms) > 0)
    {
        ESP_LOGI(TAG, "capture expired");
        bus_capture_stop();
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

void bus_capture_poll(const bus_desc_t *bdescs, size_t nb, uint32_t now_ms)
{
    if (!s_capture_active)
    {
        return;
    }
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
            analyze_bus(&bdescs[b], &s_runs[b], ev, nev, now_ms);
        }
        free(ev);
        s_cap_tail = head;
    }
}

const bus_run_t *bus_capture_runs(void)
{
    return s_runs;
}
