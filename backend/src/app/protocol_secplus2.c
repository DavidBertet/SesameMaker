// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 2.0 ESP driver. See protocol_secplus2.h (note the UNTESTED marker).

#include "protocol_secplus2.h"

#include "constants.h"
#include "garage_controller.h"
#include "garage_uart.h"
#include "protocol_registry.h"
#include "secplus2.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SECPLUS2";

#define SP2_TASK_STACK 4096
#define SP2_TASK_PRIORITY 5
#define SP2_POLL_MS 25

#define SP2_ROLL_KEY "sp2_roll"
#define SP2_CLIENT_ID SECPLUS2_CLIENT_ID_DEFAULT

// Opener broadcasts STATUS on change; poll only when it goes quiet.
#define SP2_WATCHDOG_CHECK_MS 60000
#define SP2_WATCHDOG_QUIET_MS 360000
// Sync: query, wait 3 s for a reply, once jump +60 (crash may have lost
// counter increments the opener already saw), then wait for the watchdog.
#define SP2_SYNC_WAIT_MS 3000
#define SP2_SYNC_JUMP 60
// Collision guard: defer TX while the RX line reads busy (low).
#define SP2_BUSY_RETRIES 10
#define SP2_BUSY_WAIT_MS 5
// Door press phase spacing.
#define SP2_PHASE_GAP_MS 150

static TaskHandle_t s_task;
static secplus2_framer_t s_framer;
static uint32_t s_rolling;
static uint32_t s_last_status_ms;
static uint32_t s_watchdog_ms;
static bool s_sync_waiting;
static bool s_sync_jumped;
static uint32_t s_sync_deadline_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool active(void)
{
    return protocol_registry_get() == PROTOCOL_SECPLUS2;
}

static void persist_rolling(void)
{
    uint32_t v = s_rolling;
    if (write_blob(SP2_ROLL_KEY, &v, sizeof(v)) != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS persist failed, counter continues in RAM");
    }
}

// Encode + send one command frame. When consume is true the rolling code
// advances (and persists) for this frame.
static esp_err_t send_tx(secplus2_tx_t tx, bool consume)
{
    uint8_t packet[SECPLUS2_WIRELINE_LEN];
    if (secplus2_encode_command(s_rolling, SP2_CLIENT_ID, tx.command,
                                tx.payload, packet) != 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < SP2_BUSY_RETRIES; i++)
    {
        if (gpio_get_level(GARAGE_RX_GPIO) != 0)
        {
            break;
        }
        if (i == SP2_BUSY_RETRIES - 1)
        {
            ESP_LOGW(TAG, "bus busy, dropping cmd=0x%03X", tx.command);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(SP2_BUSY_WAIT_MS));
    }
    esp_err_t ret = garage_uart_send_packet(packet, sizeof(packet));
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "TX cmd=0x%03X roll=%lu", tx.command,
                 (unsigned long)s_rolling);
        if (consume)
        {
            s_rolling = (s_rolling + 1) & SECPLUS2_ROLLING_MAX;
            persist_rolling();
        }
    }
    return ret;
}

static void handle_packet(const uint8_t *packet)
{
    uint32_t rolling;
    uint64_t device;
    uint16_t command;
    uint32_t payload;
    if (secplus2_decode_command(packet, &rolling, &device, &command,
                                &payload) != 0)
    {
        ESP_LOGW(TAG, "dropping undecodable frame");
        return;
    }
    if ((device & 0xFFFFFFFFu) == SP2_CLIENT_ID)
    {
        // Our own transmission (echo drain missed it).
        return;
    }
    uint8_t nibble = (uint8_t)((payload >> 16) & 0x0Fu);
    uint8_t byte1 = (uint8_t)((payload >> 8) & 0xFFu);
    uint8_t byte2 = (uint8_t)(payload & 0xFFu);

    if (command == SECPLUS2_CMD_STATUS)
    {
        secplus2_status_t st;
        if (!secplus2_parse_status(command, nibble, byte1, byte2, &st))
        {
            return;
        }
        s_last_status_ms = now_ms();
        s_sync_waiting = false;
        ESP_LOGI(TAG, "STATUS door=%s light=%d lock=%d obs=%d",
                 secplus1_door_state_str(st.door), st.light, st.lock,
                 (int)st.obstruction);
        garage_controller_report_secplus2_status(&st);
    }
    else if (command == SECPLUS2_CMD_MOTION)
    {
        s_last_status_ms = now_ms();
        garage_controller_report_motion();
    }
    else if (command == SECPLUS2_CMD_MOTOR_ON)
    {
        // Running notice with no OFF counterpart; door motion already
        // tracks through STATUS transitions.
        ESP_LOGI(TAG, "MOTOR_ON");
    }
    else
    {
        ESP_LOGI(TAG, "RX cmd=0x%03X nib=%u b1=0x%02X b2=0x%02X", command,
                 nibble, byte1, byte2);
    }
}

static void poll_rx(uint32_t t)
{
    uint8_t byte;
    while (garage_uart_read(&byte))
    {
        garage_uart_log_rx(byte);
        uint8_t packet[SECPLUS2_WIRELINE_LEN];
        if (secplus2_framer_feed(&s_framer, byte, t, packet))
        {
            handle_packet(packet);
        }
    }
    secplus2_framer_expire(&s_framer, t);
}

static void poll_sync(uint32_t t)
{
    if (s_sync_waiting && (int32_t)(t - s_sync_deadline_ms) > 0)
    {
        if (!s_sync_jumped)
        {
            // Our counter may lag the opener (crash between TX and flash
            // write): jump ahead and ask again before giving up.
            s_rolling = (s_rolling + SP2_SYNC_JUMP) & SECPLUS2_ROLLING_MAX;
            persist_rolling();
            s_sync_jumped = true;
            s_sync_deadline_ms = t + SP2_SYNC_WAIT_MS;
            ESP_LOGW(TAG, "no STATUS reply, counter jumped +60, retrying");
            send_tx(secplus2_build_get_status(), true);
        }
        else
        {
            s_sync_waiting = false;
            ESP_LOGW(TAG, "no STATUS reply, waiting for watchdog");
        }
    }
    if ((int32_t)(t - s_watchdog_ms) > (int32_t)SP2_WATCHDOG_CHECK_MS)
    {
        s_watchdog_ms = t;
        if ((int32_t)(t - s_last_status_ms) > (int32_t)SP2_WATCHDOG_QUIET_MS)
        {
            ESP_LOGI(TAG, "watchdog: bus quiet, polling status");
            send_tx(secplus2_build_get_status(), true);
        }
    }
}

static void sp2_task(void *pv)
{
    (void)pv;
    while (1)
    {
        if (active())
        {
            uint32_t t = now_ms();
            poll_rx(t);
            poll_sync(t);
        }
        vTaskDelay(pdMS_TO_TICKS(SP2_POLL_MS));
    }
}

esp_err_t protocol_secplus2_init(void)
{
    secplus2_framer_init(&s_framer);
    s_rolling = 0;
    size_t size = sizeof(s_rolling);
    uint32_t stored = 0;
    if (read_blob(SP2_ROLL_KEY, &stored, &size) == ESP_OK &&
        size == sizeof(stored))
    {
        s_rolling = stored & SECPLUS2_ROLLING_MAX;
    }
    else
    {
        persist_rolling();
    }
    s_last_status_ms = now_ms();
    s_watchdog_ms = s_last_status_ms;
    ESP_LOGI(TAG, "init: rolling=%lu id=0x%lX", (unsigned long)s_rolling,
             (unsigned long)SP2_CLIENT_ID);
    return ESP_OK;
}

esp_err_t protocol_secplus2_start(void)
{
    if (s_task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = xTaskCreate(sp2_task, "sp2_task", SP2_TASK_STACK, NULL,
                                 SP2_TASK_PRIORITY, &s_task);
    if (ret != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    if (active())
    {
        protocol_secplus2_activate();
    }
    return ESP_OK;
}

void protocol_secplus2_activate(void)
{
    if (garage_uart_set_secplus2_mode() != ESP_OK)
    {
        ESP_LOGW(TAG, "UART mode switch failed");
    }
    protocol_secplus2_resync();
}

esp_err_t protocol_secplus2_door(uint8_t action)
{
    if (action > SECPLUS2_DOOR_STOP)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!active())
    {
        return ESP_ERR_INVALID_STATE;
    }
    // Two-phase press: arm without consuming a code, execute consuming one.
    esp_err_t ret = send_tx(secplus2_build_door(action, 0), false);
    if (ret != ESP_OK)
    {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(SP2_PHASE_GAP_MS));
    return send_tx(secplus2_build_door(action, 1), true);
}

esp_err_t protocol_secplus2_light(uint8_t action)
{
    if (action > SECPLUS2_TOGGLE)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!active())
    {
        return ESP_ERR_INVALID_STATE;
    }
    return send_tx(secplus2_build_light(action), true);
}

esp_err_t protocol_secplus2_lock(uint8_t action)
{
    if (action > SECPLUS2_TOGGLE)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!active())
    {
        return ESP_ERR_INVALID_STATE;
    }
    return send_tx(secplus2_build_lock(action), true);
}

void protocol_secplus2_resync(void)
{
    s_sync_waiting = true;
    s_sync_jumped = false;
    s_sync_deadline_ms = now_ms() + SP2_SYNC_WAIT_MS;
    if (active())
    {
        send_tx(secplus2_build_get_status(), true);
    }
}
