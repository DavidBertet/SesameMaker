// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage door controller: runs the secplus1 bus loop, keeps door/light/
// lock/obstruction state, executes press/release command pairs with the
// proper spacing, pursues open/close/stop targets with chained toggles,
// and emulates a wall panel when none is detected.

#include "garage_controller.h"

#include "constants.h"
#include "garage_uart.h"
#include "mqtt.h"
#include "zigbee.h"
#include "protocol_drycontact.h"
#include "protocol_registry.h"
#include "storage.h"
#include "websocket.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "GARAGE_CTRL";

#define CONTROLLER_TASK_STACK 4096
#define CONTROLLER_TASK_PRIORITY 5
#define TX_QUEUE_LEN 16

typedef struct
{
    uint8_t byte;
    uint32_t at_ms; // absolute send time
} tx_item_t;

static garage_state_t s_state;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;

// Pending transmissions, press/release pairs get enqueued with delays.
static tx_item_t s_tx_queue[TX_QUEUE_LEN];
static uint8_t s_tx_count;

static secplus1_rx_parser_t s_parser;
static secplus1_door_target_t s_target = SECPLUS1_TARGET_NONE;

// Set when we transmit a status query (0x38/0x39/0x3A): every completed
// packet until s_self_reply_until_ms (the whole opener reply window, not just
// the first byte) is the opener's response to OUR question and must not be
// counted as evidence of a real wall panel. Without this, trailing bytes of a
// multi-byte reply frame would arrive after the old one-shot flag cleared and
// read as a panel polling the bus itself.
static uint32_t s_self_reply_until_ms;

// The status query we most recently sent. The opener answers by echoing that
// byte back and then the payload; the echoed byte is OUR transmission content
// (not opener data) so it is kept out of the raw RX log (and hence the
// protocol inspector), while still being fed to the parser so the natural
// [query-echo, payload] pairing works.
static uint8_t s_reply_echo_byte;
static uint32_t s_reply_echo_sent_ms;

// Panel-detection corroboration while WAITING: require TWO non-self packets
// within SECPLUS1_PANEL_EVIDENCE_WINDOW_MS before declaring a wall panel.
// A real panel polls continuously, so this confirms within a second; the two
// packets that triggered the false "detected" were spaced apart by random bus
// noise/reply debris and never corroborate. Keeps a bus without a wall panel
// from flipping into DETECTED.
static bool s_panel_cand;
static uint32_t s_panel_cand_at_ms;

static uint32_t s_last_tx_ms;
static uint32_t s_last_rx_ms;
static uint32_t s_panel_start_ms;
static uint32_t s_panel_emu_index;
static int s_last_rx_gpio_level = -1;

// Fingerprint of the state fields exposed in garage_status, used to push a
// status broadcast the moment anything changes instead of waiting up to a
// full periodic broadcast tick (~1 s) for it to reach web and MQTT clients.
typedef struct
{
    int8_t door;
    uint8_t moving;
    int8_t light;
    int8_t lock;
    uint8_t obstruction;
    uint8_t motion;
    int8_t panel;
    int8_t protocol;
    uint8_t sensors_valid;
    uint8_t open_limit;
    uint8_t close_limit;
} status_fp_t;

static status_fp_t s_last_fp;
static bool s_last_fp_valid = false;

static void take_state(void)
{
    if (s_mutex)
        xSemaphoreTake(s_mutex, portMAX_DELAY);
}
static void give_state(void)
{
    if (s_mutex)
        xSemaphoreGive(s_mutex);
}

static void enqueue_tx(uint8_t byte, uint32_t delay_ms)
{
    if (s_tx_count >= TX_QUEUE_LEN)
    {
        ESP_LOGW(TAG, "TX queue full, dropping 0x%02X", byte);
        return;
    }
    s_tx_queue[s_tx_count].byte = byte;
    s_tx_queue[s_tx_count].at_ms = garage_uart_now_ms() + delay_ms;
    s_tx_count++;
}

// Enqueue the press byte now and its release later.
static void enqueue_command_pair(uint8_t press)
{
    uint32_t release_delay;
    uint8_t release;
    switch (press)
    {
    case SECPLUS1_CMD_TOGGLE_DOOR_PRESS:
        release = SECPLUS1_CMD_TOGGLE_DOOR_RELEASE;
        release_delay = SECPLUS1_DOOR_RELEASE_DELAY_MS;
        break;
    case SECPLUS1_CMD_TOGGLE_LIGHT_PRESS:
        release = SECPLUS1_CMD_TOGGLE_LIGHT_RELEASE;
        release_delay = SECPLUS1_LIGHT_RELEASE_DELAY_MS;
        break;
    case SECPLUS1_CMD_TOGGLE_LOCK_PRESS:
    default:
        release = SECPLUS1_CMD_TOGGLE_LOCK_RELEASE;
        release_delay = SECPLUS1_LOCK_RELEASE_DELAY_MS;
        break;
    }
    enqueue_tx(press, 0);
    enqueue_tx(release, release_delay);
}

static void toggle_door(void)
{
    enqueue_command_pair(SECPLUS1_CMD_TOGGLE_DOOR_PRESS);
    // Ask for door status right after a toggle.
    enqueue_tx(SECPLUS1_CMD_QUERY_DOOR_STATUS,
               SECPLUS1_DOOR_RELEASE_DELAY_MS + SECPLUS1_TX_SPACING_MS);
    if (secplus1_door_state_is_settled(s_state.door_state) ||
        s_state.door_state == SECPLUS1_DOOR_UNKNOWN)
    {
        s_state.door_moving = true;
    }
}

static void toggle_light(void)
{
    enqueue_command_pair(SECPLUS1_CMD_TOGGLE_LIGHT_PRESS);
}

static void toggle_lock(void)
{
    enqueue_command_pair(SECPLUS1_CMD_TOGGLE_LOCK_PRESS);
}

static void apply_door_state(secplus1_door_state_t observed)
{
    if (s_state.door_state != observed)
    {
        ESP_LOGI(TAG, "Door state: %s", secplus1_door_state_str(observed));
    }
    s_state.door_state = observed;
    if (secplus1_door_state_is_settled(observed))
    {
        s_state.door_moving = false;
    }

    // Pursue an active open/close/stop target with chained toggles.
    if (s_target != SECPLUS1_TARGET_NONE)
    {
        bool done = false;
        if (secplus1_pursue_toggle_needed(observed, s_target, &done))
        {
            toggle_door();
        }
        if (done)
        {
            s_target = SECPLUS1_TARGET_NONE;
        }
    }
}

static bool self_reply_active(uint32_t now_ms)
{
    // Signed subtraction makes this immune to the 49-day uint32 wrap.
    return (int32_t)(now_ms - s_self_reply_until_ms) <= 0;
}

// Only count bus traffic as evidence of a real wall panel while we are in
// WAITING, and never count the opener's reply to our own query (that is
// self-produced traffic, not a panel). Corroborate: a single stray frame
// (noise, reply debris) must not flip us into DETECTED - two non-self
// packets close together means something really is polling the bus.
static void note_panel_evidence(uint32_t now_ms, const secplus1_rx_cmd_t *cmd)
{
    if (s_state.panel_mode != GARAGE_PANEL_WAITING)
    {
        return;
    }
    if (!s_panel_cand)
    {
        s_panel_cand = true;
        s_panel_cand_at_ms = now_ms;
        ESP_LOGI(TAG, "Panel evidence #1: type=%d resp=0x%02X now=%lu",
                 cmd->type, cmd->resp, (unsigned long)now_ms);
        return;
    }
    if ((int32_t)(now_ms - s_panel_cand_at_ms) <=
        (int32_t)SECPLUS1_PANEL_EVIDENCE_WINDOW_MS)
    {
        if (cmd->type == SECPLUS1_RX_QUERY_DOOR_STATUS_0X37)
        {
            s_state.is_0x37_panel = true; // confirm the 0x37 quirk on evidence
        }
        s_state.panel_mode = GARAGE_PANEL_DETECTED;
        s_panel_cand = false;
        ESP_LOGW(TAG, "Wall panel detected (type=%d resp=0x%02X)", cmd->type,
                 cmd->resp);
    }
    else
    {
        // First candidate went stale; count this one as the new first.
        s_panel_cand_at_ms = now_ms;
        ESP_LOGI(TAG, "Panel evidence #2 too late: type=%d resp=0x%02X",
                 cmd->type, cmd->resp);
    }
}

static void handle_rx(const secplus1_rx_cmd_t *cmd, bool self_reply)
{
    bool detect_allowed = (s_state.panel_mode == GARAGE_PANEL_WAITING &&
                           !self_reply);

    switch (cmd->type)
    {
    case SECPLUS1_RX_QUERY_DOOR_STATUS_0X37:
        if (detect_allowed)
            note_panel_evidence(garage_uart_now_ms(), cmd);
        break;
    case SECPLUS1_RX_QUERY_DOOR_STATUS: {
        secplus1_door_state_t observed = secplus1_decode_door_state(cmd->resp);
        s_state.last_status_ms = garage_uart_now_ms();
        if (!s_state.is_0x37_panel)
        {
            // Confirm after two identical observations (debounce).
            if (observed != s_state.maybe_door_state)
            {
                s_state.maybe_door_state = observed;
                break;
            }
        }
        apply_door_state(observed);
        break;
    }
    case SECPLUS1_RX_QUERY_OTHER_STATUS: {
        bool light_on, locked;
        secplus1_decode_other_status(cmd->resp, &light_on, &locked);
        s_state.light_state = light_on ? GARAGE_LIGHT_ON : GARAGE_LIGHT_OFF;
        s_state.lock_state = locked ? GARAGE_LOCK_LOCKED : GARAGE_LOCK_UNLOCKED;
        if (detect_allowed && !s_state.is_0x37_panel)
        {
            note_panel_evidence(garage_uart_now_ms(), cmd);
        }
        break;
    }
    case SECPLUS1_RX_OBSTRUCTION: {
        bool obstructed = cmd->resp != 0;
        if (obstructed != s_state.obstruction)
        {
            ESP_LOGW(TAG, "Obstruction: %s", obstructed ? "yes" : "no");
        }
        s_state.obstruction = obstructed;
        break;
    }
    case SECPLUS1_RX_TOGGLE_LIGHT_PRESS:
        // Motion on the panel also looks like a light press.
        if (s_state.light_state == GARAGE_LIGHT_OFF)
        {
            s_state.motion = true;
        }
        if (detect_allowed)
            note_panel_evidence(garage_uart_now_ms(), cmd);
        break;
    case SECPLUS1_RX_TOGGLE_DOOR_PRESS:
    case SECPLUS1_RX_TOGGLE_DOOR_RELEASE:
    case SECPLUS1_RX_TOGGLE_LIGHT_RELEASE:
    case SECPLUS1_RX_TOGGLE_LOCK_PRESS:
    case SECPLUS1_RX_TOGGLE_LOCK_RELEASE:
        if (detect_allowed)
            note_panel_evidence(garage_uart_now_ms(), cmd);
        break;
    default:
        break;
    }
}

static void poll_rx(void)
{
    uint8_t byte;
    while (garage_uart_read(&byte))
    {
        uint32_t now_ms = garage_uart_now_ms();
        s_last_rx_ms = now_ms;

        // The opener's answer to our status query echoes the query byte back
        // before the payload. That echoed byte is OUR transmission content,
        // not opener data, so suppress it from the raw RX log (which feeds
        // the protocol inspector); the payload byte afterwards is what the
        // opener actually sent. The parser still needs the echoed byte for
        // the natural query/payload pairing, so it is fed unconditionally.
        bool reply_echo =
            s_reply_echo_sent_ms != 0 &&
            secplus1_rx_is_self_echo(byte, s_reply_echo_byte, now_ms,
                                     s_reply_echo_sent_ms,
                                     SECPLUS1_SELF_REPLY_WINDOW_MS);
        if (!reply_echo)
        {
            garage_uart_log_rx(byte);
        }

        secplus1_rx_cmd_t cmd;
        if (secplus1_rx_parser_feed(&s_parser, byte, s_last_rx_ms, &cmd))
        {
            // A completed packet inside the reply window after one of our own
            // queries is the opener's response to us (it answers a status
            // query by echoing the query byte back followed by the payload) -
            // keep it out of wall-panel detection. The window (not a one-shot
            // flag) spans the whole response frame so both bytes stay tagged.
            bool self_reply = self_reply_active(now_ms);
            handle_rx(&cmd, self_reply);
        }
    }
    secplus1_rx_parser_expire(&s_parser, garage_uart_now_ms());
}

static bool tx_due(uint32_t now_ms)
{
    if (s_tx_count == 0)
    {
        return false;
    }
    if (now_ms - s_last_tx_ms < SECPLUS1_TX_SPACING_MS)
    {
        return false;
    }
    if (s_last_rx_ms != 0 && now_ms - s_last_rx_ms < SECPLUS1_TX_AFTER_RX_MS)
    {
        return false;
    }
    if (now_ms < s_tx_queue[0].at_ms)
    {
        return false;
    }
    // Line-busy guard: the RX pad idles high; a low level means a byte is
    // in flight on the wall line (e.g. an unsolicited opener transmission
    // between our polls). Keying the bus now would collide and corrupt
    // both frames, so wait a tick.
    if (gpio_get_level(GARAGE_RX_GPIO) == 0)
    {
        return false;
    }
    return true;
}

static void do_tx(void)
{
    uint8_t byte = s_tx_queue[0].byte;
    memmove(&s_tx_queue[0], &s_tx_queue[1], sizeof(tx_item_t) * (s_tx_count - 1));
    s_tx_count--;

    garage_uart_send(byte);
    s_last_tx_ms = garage_uart_now_ms();
    // The opener answers a status query (0x38/0x39/0x3A) by echoing the query
    // byte back followed by the payload; the parser pairs them naturally,
    // exactly as it does for a real wall panel. Arm the reply window so that
    // response pair (both bytes) is tagged as ours and can't be mistaken for
    // panel evidence. No synthetic prime: earlier, priming the parser made it
    // consume the opener's echoed query as the payload, which decoded a door
    // as "stopped", light as "off" and obstruction as "true".
    if (byte == SECPLUS1_CMD_QUERY_DOOR_STATUS ||
        byte == SECPLUS1_CMD_OBSTRUCTION ||
        byte == SECPLUS1_CMD_QUERY_OTHER_STATUS)
    {
        s_self_reply_until_ms = s_last_tx_ms + SECPLUS1_SELF_REPLY_WINDOW_MS;
        s_reply_echo_byte = byte;
        s_reply_echo_sent_ms = s_last_tx_ms;
    }
}

static void run_panel_logic(uint32_t now_ms)
{
    if (s_state.panel_mode == GARAGE_PANEL_WAITING)
    {
        if (now_ms - s_panel_start_ms > SECPLUS1_PANEL_DETECT_TIMEOUT_MS)
        {
            ESP_LOGI(TAG, "No wall panel found, starting emulation");
            s_state.panel_mode = GARAGE_PANEL_EMULATING;
            s_panel_emu_index = 0;
        }
        else if (now_ms - s_last_tx_ms >= SECPLUS1_PANEL_EMU_INTERVAL_MS &&
                 s_tx_count == 0)
        {
            // While still listening for a real panel, poll the opener for
            // status anyway so the door state is known immediately instead
            // of staying "unknown"/loading for the whole 35s detect window.
            // Use ONLY status queries (0x38/0x3A/0x39), never toggle presses -
            // the opener's replies are tagged self-replies and can't be
            // mistaken for a real panel's unsolicited traffic.
            enqueue_tx(secplus1_status_poll_byte(s_panel_emu_index++), 0);
        }
        return;
    }
    if (s_state.panel_mode == GARAGE_PANEL_EMULATING &&
        now_ms - s_last_tx_ms >= SECPLUS1_PANEL_EMU_INTERVAL_MS)
    {
        // Emulate the panel polling loop unless a real command is pending.
        if (s_tx_count == 0)
        {
            enqueue_tx(secplus1_panel_emu_byte(s_panel_emu_index++), 0);
        }
    }
}

// NOTE: no extra status query layer on top of the panel emulation loop -
// it already polls 0x38/0x3A/0x39/0x3A every 250 ms exactly like a real
// wall panel. Double-polling while the door moved wedged the bus into
// 0x00/0xFF garbage replies.

static status_fp_t status_fingerprint(void)
{
    status_fp_t fp;
    fp.door = (int8_t)s_state.door_state;
    fp.moving = s_state.door_moving ? 1 : 0;
    fp.light = (int8_t)s_state.light_state;
    fp.lock = (int8_t)s_state.lock_state;
    fp.obstruction = s_state.obstruction ? 1 : 0;
    fp.motion = s_state.motion ? 1 : 0;
    fp.panel = (int8_t)s_state.panel_mode;
    fp.protocol = (int8_t)s_state.protocol;
    fp.sensors_valid = s_state.sensors_valid ? 1 : 0;
    fp.open_limit = s_state.open_limit ? 1 : 0;
    fp.close_limit = s_state.close_limit ? 1 : 0;
    return fp;
}

void garage_controller_refresh_protocol(void)
{
    take_state();
    s_state.protocol = protocol_registry_get();
    s_state.caps = protocol_registry_caps();
    // A new protocol means new truth: drop bus state, flush any queued bus
    // bytes, and re-run wall-panel detection if we are back on secplus1.
    // The dry driver re-reports its reed snapshot on its next poll.
    s_state.door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.maybe_door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.light_state = GARAGE_LIGHT_UNKNOWN;
    s_state.lock_state = GARAGE_LOCK_UNKNOWN;
    s_state.obstruction = false;
    s_state.motion = false;
    s_state.door_moving = false;
    s_state.panel_mode = GARAGE_PANEL_WAITING;
    s_state.is_0x37_panel = false;
    s_state.open_limit = false;
    s_state.close_limit = false;
    s_state.sensors_valid = false;
    s_tx_count = 0;
    s_target = SECPLUS1_TARGET_NONE;
    s_panel_start_ms = garage_uart_now_ms();
    s_panel_cand = false;
    give_state();
}

// Single builder for every garage_status frame so single-user replies,
// broadcasts and MQTT-driven snapshots never drift apart.
size_t garage_controller_get_status_json(char *buf, size_t len)
{
    take_state();
    garage_state_t st = s_state;
    give_state();

    char caps[160];
    protocol_caps_json(&st.caps, caps, sizeof(caps));
    const char *panel =
        st.caps.panel
            ? (st.panel_mode == GARAGE_PANEL_DETECTED
                   ? "detected"
               : st.panel_mode == GARAGE_PANEL_EMULATING ? "emulated"
                                                         : "waiting")
            : "none";
    return (size_t)snprintf(
        buf, len,
        "{\"type\":\"garage_status\","
        "\"protocol\":\"%s\",\"caps\":{%s},"
        "\"door\":\"%s\",\"moving\":%s,"
        "\"light\":\"%s\",\"locked\":\"%s\","
        "\"obstruction\":%s,\"motion\":%s,"
        "\"panel\":\"%s\","
        "\"sensors\":{\"open\":%s,\"close\":%s,\"valid\":%s}}",
        protocol_id_str(st.protocol), caps,
        secplus1_door_state_str(st.door_state),
        st.door_moving ? "true" : "false",
        st.light_state == GARAGE_LIGHT_ON
            ? "on"
            : st.light_state == GARAGE_LIGHT_OFF ? "off" : "unknown",
        st.lock_state == GARAGE_LOCK_LOCKED
            ? "locked"
            : st.lock_state == GARAGE_LOCK_UNLOCKED ? "unlocked" : "unknown",
        st.obstruction ? "true" : "false", st.motion ? "true" : "false", panel,
        st.open_limit ? "true" : "false", st.close_limit ? "true" : "false",
        st.sensors_valid ? "true" : "false");
}

static void broadcast_status(void)
{
    take_state();
    status_fp_t fp = status_fingerprint();
    if (s_last_fp_valid && memcmp(&s_last_fp, &fp, sizeof(status_fp_t)) == 0)
    {
        // Nothing changed since the last push: skip both the websocket
        // broadcast and the MQTT publish. No periodic same-value resend -
        // clients fetch the current state on (re)connect themselves.
        give_state();
        return;
    }
    s_last_fp = fp;
    s_last_fp_valid = true;
    give_state();

    char json[512];
    garage_controller_get_status_json(json, sizeof(json));
    broadcast_message(json);
    mqtt_notify_changed();
    zigbee_report_state();
}

static void garage_task(void *pv)
{
    (void)pv;
    while (1)
    {
        // Bus protocols own this loop; dry-contact has its own task and the
        // wall bus stays idle (the opener on the other end is a dumb relay
        // pair, not a secplus device).
        take_state();
        bool bus_active = s_state.protocol == PROTOCOL_SECPLUS1;
        give_state();
        if (bus_active)
        {
            uint32_t now_ms = garage_uart_now_ms();

            int rx_gpio_level = gpio_get_level(GARAGE_RX_GPIO);
            if (rx_gpio_level != s_last_rx_gpio_level)
            {
                s_last_rx_gpio_level = rx_gpio_level;
                ESP_LOGI(TAG, "GPIO%d level=%d (RX)", GARAGE_RX_GPIO,
                         rx_gpio_level);
            }

            take_state();
            poll_rx();
            run_panel_logic(now_ms);
            if (tx_due(now_ms))
            {
                do_tx();
            }
            give_state();
        }

        // broadcast_status() no-ops unless a monitored value actually
        // changed, so the websocket and MQTT only ever receive state that
        // is different from the last push. Runs for every protocol: dry
        // reports arrive via the report setters from the dry task.
        broadcast_status();

        vTaskDelay(pdMS_TO_TICKS(GARAGE_TICK_MS));
    }
}

esp_err_t garage_controller_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.maybe_door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.light_state = GARAGE_LIGHT_UNKNOWN;
    s_state.lock_state = GARAGE_LOCK_UNKNOWN;
    s_state.panel_mode = GARAGE_PANEL_WAITING;
    s_state.last_status_ms = garage_uart_now_ms();
    s_state.protocol = protocol_registry_get();
    s_state.caps = protocol_registry_caps();
    s_state.open_limit = false;
    s_state.close_limit = false;
    s_state.sensors_valid = false;

    secplus1_rx_parser_init(&s_parser);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = protocol_drycontact_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    return garage_uart_init();
}

esp_err_t garage_controller_start(void)
{
    if (s_task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_panel_start_ms = garage_uart_now_ms();

    BaseType_t ret = xTaskCreate(garage_task, "garage_task",
                                 CONTROLLER_TASK_STACK, NULL,
                                 CONTROLLER_TASK_PRIORITY, &s_task);
    if (ret != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Garage controller started");
    return protocol_drycontact_start();
}

esp_err_t garage_controller_door_action(const char *action)
{
    if (!action)
    {
        return ESP_ERR_INVALID_ARG;
    }
    take_state();
    if (s_state.protocol == PROTOCOL_DRYCONTACT)
    {
        // Delegate without holding the lock: the driver reads snapshots via
        // get_state and reports back through the setters (lock ordering).
        int current = (int)s_state.door_state;
        give_state();
        int target;
        if (strcmp(action, "toggle") == 0)
        {
            target = DRY_TARGET_TOGGLE;
        }
        else if (strcmp(action, "open") == 0)
        {
            target = DRY_DOOR_OPEN;
        }
        else if (strcmp(action, "close") == 0)
        {
            target = DRY_DOOR_CLOSED;
        }
        else if (strcmp(action, "stop") == 0)
        {
            target = DRY_DOOR_STOPPED;
        }
        else
        {
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t ret = protocol_drycontact_command(target, current);
        broadcast_status();
        return ret;
    }
    if (s_state.protocol != PROTOCOL_SECPLUS1)
    {
        // No driver behind the active protocol yet (secplus2 stub).
        give_state();
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(action, "toggle") == 0)
    {
        s_target = SECPLUS1_TARGET_NONE;
        toggle_door();
    }
    else if (strcmp(action, "open") == 0)
    {
        s_target = SECPLUS1_TARGET_OPEN;
        bool done = false;
        if (secplus1_pursue_toggle_needed(s_state.door_state, s_target, &done))
        {
            toggle_door();
        }
        if (done)
            s_target = SECPLUS1_TARGET_NONE;
    }
    else if (strcmp(action, "close") == 0)
    {
        s_target = SECPLUS1_TARGET_CLOSE;
        bool done = false;
        if (secplus1_pursue_toggle_needed(s_state.door_state, s_target, &done))
        {
            toggle_door();
        }
        if (done)
            s_target = SECPLUS1_TARGET_NONE;
    }
    else if (strcmp(action, "stop") == 0)
    {
        s_target = SECPLUS1_TARGET_STOP;
        bool done = false;
        if (secplus1_pursue_toggle_needed(s_state.door_state, s_target, &done))
        {
            toggle_door();
        }
        if (done)
            s_target = SECPLUS1_TARGET_NONE;
    }
    else
    {
        give_state();
        return ESP_ERR_INVALID_ARG;
    }
    give_state();
    broadcast_status();
    return ESP_OK;
}

esp_err_t garage_controller_light_action(const char *action)
{
    if (!action)
    {
        return ESP_ERR_INVALID_ARG;
    }
    take_state();
    if (!s_state.caps.light)
    {
        give_state();
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(action, "toggle") == 0 ||
        (strcmp(action, "on") == 0 && s_state.light_state != GARAGE_LIGHT_ON) ||
        (strcmp(action, "off") == 0 && s_state.light_state != GARAGE_LIGHT_OFF))
    {
        toggle_light();
    }
    give_state();
    broadcast_status();
    return ESP_OK;
}

esp_err_t garage_controller_lock_action(const char *action)
{
    if (!action)
    {
        return ESP_ERR_INVALID_ARG;
    }
    take_state();
    if (!s_state.caps.lock)
    {
        give_state();
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(action, "toggle") == 0 ||
        (strcmp(action, "lock") == 0 && s_state.lock_state != GARAGE_LOCK_LOCKED) ||
        (strcmp(action, "unlock") == 0 && s_state.lock_state != GARAGE_LOCK_UNLOCKED))
    {
        toggle_lock();
    }
    give_state();
    broadcast_status();
    return ESP_OK;
}

esp_err_t garage_controller_get_state(garage_state_t *out)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }
    take_state();
    *out = s_state;
    give_state();
    return ESP_OK;
}

void garage_controller_report_door(secplus1_door_state_t door, bool moving)
{
    take_state();
    if (s_state.door_state != door)
    {
        ESP_LOGI(TAG, "Door state: %s", secplus1_door_state_str(door));
    }
    s_state.door_state = door;
    s_state.door_moving = moving;
    give_state();
}

void garage_controller_report_sensors(bool open_hit, bool close_hit, bool valid)
{
    take_state();
    s_state.open_limit = open_hit;
    s_state.close_limit = close_hit;
    s_state.sensors_valid = valid;
    give_state();
}

esp_err_t garage_controller_sync(void)
{
    take_state();
    bool is_dry = s_state.protocol == PROTOCOL_DRYCONTACT;
    give_state();
    if (is_dry)
    {
        // No bus to query: re-sample the reeds and let the fingerprint push
        // a frame if anything changed.
        protocol_drycontact_resync();
        broadcast_status();
        return ESP_OK;
    }
    take_state();
    s_state.door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.maybe_door_state = SECPLUS1_DOOR_UNKNOWN;
    s_state.light_state = GARAGE_LIGHT_UNKNOWN;
    s_state.lock_state = GARAGE_LOCK_UNKNOWN;
    // Two door queries back to back so the 2-sample door debounce confirms on
    // the second reply instead of waiting for the emulation wheel to roll
    // back around to 0x38 (up to ~4 s). Other-status for light/lock goes in
    // between. (Beware the ~200 ms secplus1 TX spacing + ~50 ms quiet-after-
    // RX guards: these at_ms values sit comfortably beyond them.)
    enqueue_tx(SECPLUS1_CMD_QUERY_DOOR_STATUS, 0);
    enqueue_tx(SECPLUS1_CMD_QUERY_OTHER_STATUS,
               SECPLUS1_TX_SPACING_MS + 20);
    enqueue_tx(SECPLUS1_CMD_QUERY_DOOR_STATUS,
               SECPLUS1_TX_SPACING_MS * 2 + 40);
    give_state();
    return ESP_OK;
}

bool garage_controller_light_on(void)
{
    return s_state.light_state == GARAGE_LIGHT_ON;
}

bool garage_controller_locked(void)
{
    return s_state.lock_state == GARAGE_LOCK_LOCKED;
}
