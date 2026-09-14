// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Dry-contact ESP driver: relay pulse output + reed limit inputs with
// debounce, travel-timeout motion inference, and NVS config. Reports into
// the garage controller via its report setters; the controller owns state,
// broadcast and MQTT.

#include "protocol_drycontact.h"

#include "constants.h"
#include "garage_controller.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "DRY";

#define DRY_TASK_STACK 3072
#define DRY_TASK_PRIORITY 5
#define DRY_POLL_MS 25

#define DRY_CFG_KEY "dry_cfg"

static dry_cfg_t s_cfg;
static TaskHandle_t s_task;

// Debounce bookkeeping per reed.
static bool s_open_stable, s_open_raw;
static uint32_t s_open_changed_ms;
static bool s_close_stable, s_close_raw;
static uint32_t s_close_changed_ms;

// Last limit seen hit (for the in-between inference) + motion tracking.
static bool s_last_open, s_last_close;
static bool s_moving;
static int s_inferred = DRY_DOOR_UNKNOWN; // DRY_DOOR_* while moving w/o limits
static uint32_t s_pulse_at_ms;
static bool s_pulse_reported_opening; // direction assumed after last pulse
static esp_timer_handle_t s_pulse_timer; // single one-shot, restarted per press

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void dry_cfg_defaults(dry_cfg_t *out)
{
    if (!out)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->relay_gpio = DRY_RELAY_GPIO;
    out->open_gpio = DRY_OPEN_LIMIT_GPIO;
    out->close_gpio = DRY_CLOSE_LIMIT_GPIO;
    out->sensor_mode = DRY_SENSORS_BOTH;
    out->active_low = 1;
    out->pulse_ms = DRY_PULSE_MS_DEFAULT;
    out->debounce_ms = DRY_DEBOUNCE_MS_DEFAULT;
    out->travel_s = DRY_TRAVEL_S_DEFAULT;
}

static bool gpio_valid(int8_t g)
{
    return g >= 0 && g < GPIO_NUM_MAX && drycontact_gpio_allowed(g);
}

static bool cfg_valid(const dry_cfg_t *c)
{
    if (!c || !gpio_valid(c->relay_gpio))
    {
        return false;
    }
    if (c->sensor_mode > DRY_SENSORS_BOTH)
    {
        return false;
    }
    if (c->sensor_mode != DRY_SENSORS_NONE)
    {
        int8_t first = c->sensor_mode == DRY_SENSORS_BOTH ? c->open_gpio : c->close_gpio;
        if (!gpio_valid(first))
        {
            return false;
        }
        if (c->sensor_mode == DRY_SENSORS_BOTH && !gpio_valid(c->close_gpio))
        {
            return false;
        }
    }
    if (c->pulse_ms < 100 || c->pulse_ms > 5000)
    {
        return false;
    }
    if (c->debounce_ms > 1000)
    {
        return false;
    }
    if (c->travel_s < 5 || c->travel_s > 300)
    {
        return false;
    }
    return true;
}

static void apply_gpios(void)
{
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << s_cfg.relay_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_set_level(s_cfg.relay_gpio, 0);

    gpio_config_t in = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    uint64_t mask = 0;
    if (s_cfg.sensor_mode == DRY_SENSORS_BOTH)
    {
        mask |= 1ULL << s_cfg.open_gpio;
        mask |= 1ULL << s_cfg.close_gpio;
    }
    else if (s_cfg.sensor_mode == DRY_SENSORS_CLOSE_ONLY)
    {
        mask |= 1ULL << s_cfg.close_gpio;
    }
    if (mask)
    {
        in.pin_bit_mask = mask;
        gpio_config(&in);
    }
}

// Raw electrical level -> normalized "hit" (active-level applied).
static bool sample_hit(int8_t gpio)
{
    int level = gpio_get_level((gpio_num_t)gpio);
    bool high = level != 0;
    return s_cfg.active_low ? !high : high;
}

static void report_snapshot(void)
{
    int door;
    bool open_hit = false, close_hit = false;
    if (s_cfg.sensor_mode == DRY_SENSORS_BOTH)
    {
        open_hit = s_open_stable;
        close_hit = s_close_stable;
        if (open_hit)
        {
            s_last_open = true;
            s_last_close = false;
        }
        else if (close_hit)
        {
            s_last_open = false;
            s_last_close = true;
        }
        door = drycontact_infer_both(open_hit, close_hit, s_last_open, s_last_close);
    }
    else if (s_cfg.sensor_mode == DRY_SENSORS_CLOSE_ONLY)
    {
        close_hit = s_close_stable;
        if (close_hit)
        {
            s_last_close = true;
        }
        else
        {
            s_last_close = false;
        }
        door = drycontact_infer_close_only(close_hit);
    }
    else
    {
        door = DRY_DOOR_UNKNOWN;
    }

    garage_controller_report_sensors(open_hit, close_hit,
                                     s_cfg.sensor_mode != DRY_SENSORS_NONE);

    if (door == DRY_DOOR_OPEN || door == DRY_DOOR_CLOSED)
    {
        // A limit confirms the end of travel: clear any in-flight motion.
        s_moving = false;
        garage_controller_report_door((secplus1_door_state_t)door, false);
    }
    else if (!s_moving)
    {
        // Idle between limits: publish the inference (or unknown) unsettled.
        garage_controller_report_door((secplus1_door_state_t)door, false);
    }
    // While moving, the travel-timeout branch owns the reported state.
}

static void poll_reeds(uint32_t t)
{
    bool changed = false;
    if (s_cfg.sensor_mode == DRY_SENSORS_BOTH)
    {
        bool raw = sample_hit(s_cfg.open_gpio);
        changed |= drycontact_debounce(raw, t, &s_open_stable, &s_open_raw,
                                       &s_open_changed_ms, s_cfg.debounce_ms);
    }
    if (s_cfg.sensor_mode != DRY_SENSORS_NONE)
    {
        bool raw = sample_hit(s_cfg.close_gpio);
        changed |= drycontact_debounce(raw, t, &s_close_stable, &s_close_raw,
                                       &s_close_changed_ms, s_cfg.debounce_ms);
    }
    if (changed)
    {
        ESP_LOGI(TAG, "reed open=%d close=%d", (int)s_open_stable,
                 (int)s_close_stable);
        report_snapshot();
    }
}

static void poll_travel_timeout(uint32_t t)
{
    if (!s_moving)
    {
        return;
    }
    if (t - s_pulse_at_ms < (uint32_t)s_cfg.travel_s * 1000u)
    {
        return;
    }
    s_moving = false;
    if (s_cfg.sensor_mode == DRY_SENSORS_NONE)
    {
        garage_controller_report_door(SECPLUS1_DOOR_UNKNOWN, false);
    }
    else if (s_cfg.sensor_mode == DRY_SENSORS_CLOSE_ONLY)
    {
        // Never saw the close reed: assume the opening press worked.
        if (s_pulse_reported_opening)
        {
            garage_controller_report_door(SECPLUS1_DOOR_OPEN, false);
        }
        else
        {
            garage_controller_report_door(SECPLUS1_DOOR_UNKNOWN, false);
        }
    }
    else
    {
        // Both reeds fitted but neither hit in time: wedged mid-travel.
        ESP_LOGW(TAG, "travel timeout with no limit hit");
        garage_controller_report_door(SECPLUS1_DOOR_STOPPED, false);
    }
}

static void dry_task(void *pv)
{
    (void)pv;
    while (1)
    {
        uint32_t t = now_ms();
        poll_reeds(t);
        poll_travel_timeout(t);
        vTaskDelay(pdMS_TO_TICKS(DRY_POLL_MS));
    }
}

static void pulse_off_cb(void *arg)
{
    (void)arg;
    gpio_set_level(s_cfg.relay_gpio, 0);
}

esp_err_t protocol_drycontact_init(void)
{
    dry_cfg_defaults(&s_cfg);
    size_t size = 0;
    if (read_blob(DRY_CFG_KEY, NULL, &size) == ESP_OK && size == sizeof(s_cfg))
    {
        dry_cfg_t stored;
        size_t sz = sizeof(stored);
        if (read_blob(DRY_CFG_KEY, &stored, &sz) == ESP_OK && cfg_valid(&stored))
        {
            s_cfg = stored;
        }
    }
    else
    {
        write_blob(DRY_CFG_KEY, &s_cfg, sizeof(s_cfg));
    }
    apply_gpios();
    esp_timer_create_args_t pulse_args = {
        .callback = pulse_off_cb,
        .name = "dry_pulse",
    };
    esp_timer_create(&pulse_args, &s_pulse_timer);

    uint32_t t = now_ms();
    if (s_cfg.sensor_mode == DRY_SENSORS_BOTH)
    {
        s_open_raw = s_open_stable = sample_hit(s_cfg.open_gpio);
        s_open_changed_ms = t;
    }
    if (s_cfg.sensor_mode != DRY_SENSORS_NONE)
    {
        s_close_raw = s_close_stable = sample_hit(s_cfg.close_gpio);
        s_close_changed_ms = t;
    }
    report_snapshot();
    ESP_LOGI(TAG, "dry init: relay=%d mode=%d pulse=%dms",
             s_cfg.relay_gpio, s_cfg.sensor_mode, s_cfg.pulse_ms);
    return ESP_OK;
}

esp_err_t protocol_drycontact_start(void)
{
    if (s_task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = xTaskCreate(dry_task, "dry_task", DRY_TASK_STACK, NULL,
                                 DRY_TASK_PRIORITY, &s_task);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t protocol_drycontact_command(int target, int current)
{
    bool done = false;
    if (!drycontact_pulse_needed(current, target, &done))
    {
        return ESP_OK; // already there / on its way / no-op
    }
    gpio_set_level(s_cfg.relay_gpio, 1);
    if (s_pulse_timer)
    {
        esp_timer_stop(s_pulse_timer);
        esp_timer_start_once(s_pulse_timer, (uint64_t)s_cfg.pulse_ms * 1000u);
    }

    // Optimistic motion report: the press is away, expect travel.
    s_moving = true;
    s_pulse_at_ms = now_ms();
    if (target == DRY_DOOR_OPEN)
    {
        s_pulse_reported_opening = true;
        s_inferred = DRY_DOOR_OPENING;
        garage_controller_report_door(SECPLUS1_DOOR_OPENING, true);
    }
    else if (target == DRY_DOOR_CLOSED)
    {
        s_pulse_reported_opening = false;
        s_inferred = DRY_DOOR_CLOSING;
        garage_controller_report_door(SECPLUS1_DOOR_CLOSING, true);
    }
    else
    {
        s_inferred = DRY_DOOR_UNKNOWN;
        garage_controller_report_door(SECPLUS1_DOOR_UNKNOWN, true);
    }
    (void)done;
    ESP_LOGI(TAG, "pulse %dms for target=%d (was %d)", s_cfg.pulse_ms, target,
             current);
    return ESP_OK;
}

void protocol_drycontact_resync(void)
{
    uint32_t t = now_ms();
    if (s_cfg.sensor_mode == DRY_SENSORS_BOTH)
    {
        s_open_stable = s_open_raw = sample_hit(s_cfg.open_gpio);
        s_open_changed_ms = t;
    }
    if (s_cfg.sensor_mode != DRY_SENSORS_NONE)
    {
        s_close_stable = s_close_raw = sample_hit(s_cfg.close_gpio);
        s_close_changed_ms = t;
    }
    report_snapshot();
}

esp_err_t protocol_drycontact_get_cfg(dry_cfg_t *out)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_cfg;
    return ESP_OK;
}

esp_err_t protocol_drycontact_set_cfg(const dry_cfg_t *cfg)
{
    if (!cfg_valid(cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = write_blob(DRY_CFG_KEY, cfg, sizeof(*cfg));
    if (ret != ESP_OK)
    {
        return ret;
    }
    s_cfg = *cfg;
    apply_gpios();
    protocol_drycontact_resync();
    return ESP_OK;
}
