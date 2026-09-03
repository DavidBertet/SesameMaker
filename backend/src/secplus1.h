// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 1.0 ("secplus1") wired wall-control protocol - pure logic only.
// Transport is a 1200 baud 8E1 half-duplex UART on the 2-wire wall bus.
// Reference: ratgdo/esphome-ratgdo components/ratgdo/secplus1.{h,cpp}.
// No ESP-IDF includes here so it can be unit tested on the host.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ==== Command bytes ====
#define SECPLUS1_CMD_TOGGLE_DOOR_PRESS 0x30
#define SECPLUS1_CMD_TOGGLE_DOOR_RELEASE 0x31
#define SECPLUS1_CMD_TOGGLE_LIGHT_PRESS 0x32
#define SECPLUS1_CMD_TOGGLE_LIGHT_RELEASE 0x33
#define SECPLUS1_CMD_TOGGLE_LOCK_PRESS 0x34
#define SECPLUS1_CMD_TOGGLE_LOCK_RELEASE 0x35
#define SECPLUS1_CMD_QUERY_DOOR_STATUS_0X37 0x37
#define SECPLUS1_CMD_QUERY_DOOR_STATUS 0x38
#define SECPLUS1_CMD_OBSTRUCTION 0x39
#define SECPLUS1_CMD_QUERY_OTHER_STATUS 0x3A

// ==== Timing (ms) ====
#define SECPLUS1_DOOR_RELEASE_DELAY_MS 500
#define SECPLUS1_LIGHT_RELEASE_DELAY_MS 500
#define SECPLUS1_LOCK_RELEASE_DELAY_MS 3500
#define SECPLUS1_TX_SPACING_MS 200  // min gap between our TX bytes
#define SECPLUS1_TX_AFTER_RX_MS 50   // min quiet time after RX before we TX
#define SECPLUS1_PANEL_DETECT_TIMEOUT_MS 35000
#define SECPLUS1_PANEL_EMU_INTERVAL_MS 250
#define SECPLUS1_RX_PACKET_TIMEOUT_MS 100 // discard incomplete 2-byte packet
#define SECPLUS1_SELF_REPLY_WINDOW_MS 200 // response pair after our query
#define SECPLUS1_PANEL_EVIDENCE_WINDOW_MS 2000 // 2 non-self packets => panel
#define SECPLUS1_SYNC_TIMEOUT_MS 45000

typedef enum
{
    SECPLUS1_DOOR_UNKNOWN = 0,
    SECPLUS1_DOOR_OPEN,
    SECPLUS1_DOOR_CLOSED,
    SECPLUS1_DOOR_OPENING,
    SECPLUS1_DOOR_CLOSING,
    SECPLUS1_DOOR_STOPPED,
} secplus1_door_state_t;

typedef enum
{
    SECPLUS1_TARGET_NONE = 0,
    SECPLUS1_TARGET_OPEN,
    SECPLUS1_TARGET_CLOSE,
    SECPLUS1_TARGET_STOP,
    SECPLUS1_TARGET_TOGGLE,
} secplus1_door_target_t;

typedef enum
{
    SECPLUS1_RX_UNKNOWN = 0,
    SECPLUS1_RX_TOGGLE_DOOR_PRESS,
    SECPLUS1_RX_TOGGLE_DOOR_RELEASE,
    SECPLUS1_RX_TOGGLE_LIGHT_PRESS, // also: motion detected on panel
    SECPLUS1_RX_TOGGLE_LIGHT_RELEASE,
    SECPLUS1_RX_TOGGLE_LOCK_PRESS,
    SECPLUS1_RX_TOGGLE_LOCK_RELEASE,
    SECPLUS1_RX_QUERY_DOOR_STATUS_0X37,
    SECPLUS1_RX_QUERY_DOOR_STATUS,
    SECPLUS1_RX_OBSTRUCTION,
    SECPLUS1_RX_QUERY_OTHER_STATUS,
    SECPLUS1_RX_INVALID_BYTE,
} secplus1_rx_type_t;

typedef struct
{
    secplus1_rx_type_t type;
    uint8_t resp; // second byte for 2-byte packets
} secplus1_rx_cmd_t;

// Byte-stream parser: feed raw UART bytes, get decoded commands out.
typedef struct
{
    uint8_t packet[2];
    uint8_t byte_count;
    bool reading_msg;
    uint32_t last_byte_ms;
} secplus1_rx_parser_t;

void secplus1_rx_parser_init(secplus1_rx_parser_t *p);

// Feed one byte. Returns true when *out is filled with a decoded command.
// now_ms drives the incomplete-packet timeout (SECPLUS1_RX_PACKET_TIMEOUT_MS).
bool secplus1_rx_parser_feed(secplus1_rx_parser_t *p, uint8_t byte,
                             uint32_t now_ms, secplus1_rx_cmd_t *out);

// Prime the parser as if a status query byte (0x38/0x39/0x3A) had been
// received. The opener answers those queries with a single reply byte on the
// shared bus; ratgdo pairs it with the query echo it reads back on its
// single-wire transport. Our 2-pin transport discards the TX echo, so the
// caller primes here right after transmitting the query, and the reply byte
// completes the pair. Returns false for non-query bytes.
bool secplus1_rx_parser_prime(secplus1_rx_parser_t *p, uint8_t query_byte,
                              uint32_t now_ms);

// Drop a stale half-packet without consuming a byte (timeout only).
void secplus1_rx_parser_expire(secplus1_rx_parser_t *p, uint32_t now_ms);

// True when a received byte is the echo of our own just-transmitted byte on
// the shared half-duplex wall bus: same byte value, read back within
// window_ms of the moment we sent it. The controller drops these BEFORE the
// parser so an echo can never pair with its own primed status query. Without
// this, an 0x38 echo completes the primed packet with resp=0x38, which
// decodes as door "stopped" and keeps resetting the 2-sample door debounce.
bool secplus1_rx_is_self_echo(uint8_t received, uint8_t sent,
                              uint32_t now_ms, uint32_t sent_at_ms,
                              uint32_t window_ms);

// Classify a single byte as an rx type (no framing).
secplus1_rx_type_t secplus1_rx_type_for_byte(uint8_t byte);

// Decode the resp byte of a QUERY_DOOR_STATUS (0x38) reply.
secplus1_door_state_t secplus1_decode_door_state(uint8_t resp);

// Decode the resp byte of a QUERY_OTHER_STATUS (0x3A) reply.
void secplus1_decode_other_status(uint8_t resp, bool *light_on, bool *locked);

// True if the door state is a "settled" one (not moving).
bool secplus1_door_state_is_settled(secplus1_door_state_t state);

// String name ("open", "closed", ...) - static storage.
const char *secplus1_door_state_str(secplus1_door_state_t state);

// Door pursuit: decide whether to send a toggle right now to reach `target`
// given the just-confirmed current state. *done is set when the target is
// reached (or abandoned) and the sequence should be cleared.
// Mirrors ratgdo's chained-toggle door_action logic, made reactive.
bool secplus1_pursue_toggle_needed(secplus1_door_state_t current,
                                   secplus1_door_target_t target, bool *done);

// Wall panel emulation: startup sequence is 15 bytes, then loops over the
// last 4. Returns the byte to send at `index` (monotonic counter).
uint8_t secplus1_panel_emu_byte(uint32_t index);

// Byte for the WAITING-phase status-only poll loop (only 0x38/0x3A/0x39 -
// never toggle presses, so replies can't be mistaken for a real panel).
uint8_t secplus1_status_poll_byte(uint32_t index);

#ifdef __cplusplus
}
#endif
