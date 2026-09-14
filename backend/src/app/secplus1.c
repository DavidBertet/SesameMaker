// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 1.0 protocol - pure logic, host testable. See secplus1.h.

#include "secplus1.h"

#include <string.h>

// Startup: pretend to be a multifunction wall panel booting up.
// After the 15 first bytes the sequence loops over the last 4 entries,
// which polls door/obstruction/other status like a real panel would.
static const uint8_t PANEL_EMU_STATES[] = {
    0x35, 0x35, 0x35, 0x35, 0x33, 0x33, 0x53, 0x53, 0x38,
    0x3A, 0x3A, 0x3A, 0x39, 0x38, 0x3A, // startup: indexes 0..14
    0x38, 0x3A, 0x39, 0x3A,             // steady state loop: indexes 15..18
};
#define PANEL_EMU_LOOP_START 15u
#define PANEL_EMU_LOOP_LEN 4u

secplus1_rx_type_t secplus1_rx_type_for_byte(uint8_t byte)
{
    switch (byte)
    {
    case SECPLUS1_CMD_TOGGLE_DOOR_PRESS:
        return SECPLUS1_RX_TOGGLE_DOOR_PRESS;
    case SECPLUS1_CMD_TOGGLE_DOOR_RELEASE:
        return SECPLUS1_RX_TOGGLE_DOOR_RELEASE;
    case SECPLUS1_CMD_TOGGLE_LIGHT_PRESS:
        return SECPLUS1_RX_TOGGLE_LIGHT_PRESS;
    case SECPLUS1_CMD_TOGGLE_LIGHT_RELEASE:
        return SECPLUS1_RX_TOGGLE_LIGHT_RELEASE;
    case SECPLUS1_CMD_TOGGLE_LOCK_PRESS:
        return SECPLUS1_RX_TOGGLE_LOCK_PRESS;
    case SECPLUS1_CMD_TOGGLE_LOCK_RELEASE:
        return SECPLUS1_RX_TOGGLE_LOCK_RELEASE;
    case SECPLUS1_CMD_QUERY_DOOR_STATUS_0X37:
        return SECPLUS1_RX_QUERY_DOOR_STATUS_0X37;
    case SECPLUS1_CMD_QUERY_DOOR_STATUS:
        return SECPLUS1_RX_QUERY_DOOR_STATUS;
    case SECPLUS1_CMD_OBSTRUCTION:
        return SECPLUS1_RX_OBSTRUCTION;
    case SECPLUS1_CMD_QUERY_OTHER_STATUS:
        return SECPLUS1_RX_QUERY_OTHER_STATUS;
    default:
        return SECPLUS1_RX_INVALID_BYTE;
    }
}

void secplus1_rx_parser_init(secplus1_rx_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

void secplus1_rx_parser_expire(secplus1_rx_parser_t *p, uint32_t now_ms)
{
    if (p->reading_msg &&
        now_ms - p->last_byte_ms > SECPLUS1_RX_PACKET_TIMEOUT_MS)
    {
        // The rest of the packet is not coming; drop the half-packet.
        p->reading_msg = false;
        p->byte_count = 0;
    }
}

bool secplus1_rx_is_self_echo(uint8_t received, uint8_t sent,
                              uint32_t now_ms, uint32_t sent_at_ms,
                              uint32_t window_ms)
{
    if (received != sent)
    {
        return false;
    }
    return now_ms - sent_at_ms <= window_ms;
}

bool secplus1_rx_parser_prime(secplus1_rx_parser_t *p, uint8_t query_byte,
                              uint32_t now_ms)
{
    if (query_byte != SECPLUS1_CMD_QUERY_DOOR_STATUS &&
        query_byte != SECPLUS1_CMD_OBSTRUCTION &&
        query_byte != SECPLUS1_CMD_QUERY_OTHER_STATUS)
    {
        return false;
    }
    p->packet[0] = query_byte;
    p->byte_count = 1;
    p->reading_msg = true;
    p->last_byte_ms = now_ms;
    return true;
}

bool secplus1_rx_parser_feed(secplus1_rx_parser_t *p, uint8_t byte,
                             uint32_t now_ms, secplus1_rx_cmd_t *out)
{
    memset(out, 0, sizeof(*out));

    secplus1_rx_parser_expire(p, now_ms);

    if (p->reading_msg)
    {
        p->packet[p->byte_count++] = byte;
        p->last_byte_ms = now_ms;
        p->reading_msg = false;
        p->byte_count = 0;
        out->type = secplus1_rx_type_for_byte(p->packet[0]);
        out->resp = p->packet[1];
        return true;
    }

    if (byte < 0x30 || byte > 0x3A)
    {
        out->type = SECPLUS1_RX_INVALID_BYTE;
        out->resp = byte;
        // resync
        p->byte_count = 0;
        p->reading_msg = false;
        return true;
    }

    if (byte == SECPLUS1_CMD_QUERY_DOOR_STATUS ||
        byte == SECPLUS1_CMD_OBSTRUCTION ||
        byte == SECPLUS1_CMD_QUERY_OTHER_STATUS)
    {
        // Start of a 2-byte reply: command byte + payload.
        p->packet[0] = byte;
        p->byte_count = 1;
        p->reading_msg = true;
        p->last_byte_ms = now_ms;
        return false;
    }

    // Single byte panel command (0x30..0x37).
    out->type = secplus1_rx_type_for_byte(byte);
    out->resp = 0;
    return true;
}

secplus1_door_state_t secplus1_decode_door_state(uint8_t resp)
{
    switch (resp & 0x7)
    {
    case 0x2:
        return SECPLUS1_DOOR_OPEN;
    case 0x5:
        return SECPLUS1_DOOR_CLOSED;
    case 0x0:
    case 0x6:
        return SECPLUS1_DOOR_STOPPED;
    case 0x1:
        return SECPLUS1_DOOR_OPENING;
    case 0x4:
        return SECPLUS1_DOOR_CLOSING;
    default:
        return SECPLUS1_DOOR_UNKNOWN;
    }
}

void secplus1_decode_other_status(uint8_t resp, bool *light_on, bool *locked)
{
    if (light_on)
    {
        *light_on = ((resp >> 2) & 1) != 0;
    }
    if (locked)
    {
        *locked = ((~resp >> 3) & 1) != 0;
    }
}

bool secplus1_door_state_is_settled(secplus1_door_state_t state)
{
    return state == SECPLUS1_DOOR_OPEN || state == SECPLUS1_DOOR_CLOSED ||
           state == SECPLUS1_DOOR_STOPPED;
}

const char *secplus1_door_state_str(secplus1_door_state_t state)
{
    switch (state)
    {
    case SECPLUS1_DOOR_OPEN:
        return "open";
    case SECPLUS1_DOOR_CLOSED:
        return "closed";
    case SECPLUS1_DOOR_OPENING:
        return "opening";
    case SECPLUS1_DOOR_CLOSING:
        return "closing";
    case SECPLUS1_DOOR_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

bool secplus1_pursue_toggle_needed(secplus1_door_state_t current,
                                   secplus1_door_target_t target, bool *done)
{
    if (done)
    {
        *done = false;
    }
    if (target == SECPLUS1_TARGET_NONE)
    {
        if (done)
            *done = true;
        return false;
    }
    if (target == SECPLUS1_TARGET_TOGGLE)
    {
        // Fire once and clear: a toggle always moves something.
        if (done)
            *done = true;
        return true;
    }

    switch (target)
    {
    case SECPLUS1_TARGET_OPEN:
        if (current == SECPLUS1_DOOR_OPEN)
        { // reached
            if (done)
                *done = true;
            return false;
        }
        if (current == SECPLUS1_DOOR_OPENING)
            return false; // wait
        // closed, closing or stopped: a toggle makes progress
        // (from stopped the door starts closing, the next confirmed
        // closing state will toggle again - matches ratgdo chaining)
        return true;

    case SECPLUS1_TARGET_CLOSE:
        if (current == SECPLUS1_DOOR_CLOSED)
        {
            if (done)
                *done = true;
            return false;
        }
        if (current == SECPLUS1_DOOR_CLOSING)
            return false; // wait
        return true;      // open, opening or stopped

    case SECPLUS1_TARGET_STOP:
    default:
        if (secplus1_door_state_is_settled(current))
        {
            if (done)
                *done = true;
            return false;
        }
        return true; // opening or closing
    }
}

uint8_t secplus1_panel_emu_byte(uint32_t index)
{
    if (index < PANEL_EMU_LOOP_START)
    {
        return PANEL_EMU_STATES[index];
    }
    return PANEL_EMU_STATES[PANEL_EMU_LOOP_START +
                            (index - PANEL_EMU_LOOP_START) % PANEL_EMU_LOOP_LEN];
}

// Status-only poll sequence used while we are still WAITING to detect a real
// wall panel. Unlike the full emulation boot sequence, this sends ONLY status
// query bytes (0x38/0x3A/0x39), never toggle/release presses. That way the
// opener's replies are always tagged as self-replies and can never be mistaken
// for a real panel's unsolicited traffic (which would falsely flip WAITING ->
// DETECTED). Mirrors the steady-state loop indexes 15..18.
uint8_t secplus1_status_poll_byte(uint32_t index)
{
    static const uint8_t STATUS_QUERIES[] = {
        SECPLUS1_CMD_QUERY_DOOR_STATUS,   // 0x38
        SECPLUS1_CMD_QUERY_OTHER_STATUS,  // 0x3A
        SECPLUS1_CMD_OBSTRUCTION,         // 0x39
        SECPLUS1_CMD_QUERY_OTHER_STATUS,  // 0x3A
    };
    return STATUS_QUERIES[index % (sizeof(STATUS_QUERIES) / sizeof(*STATUS_QUERIES))];
}
