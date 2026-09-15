// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 2.0 wireline codec: encodes/decodes the 19-byte wall-bus packets
// (header 55 01 00 + two 8-byte halves) used by Chamberlain/LiftMaster
// openers with a yellow learn button.
//
// Original implementation written for SesameMaker. Algorithm per Chamberlain's
// US20110317835A1 ("wireline transmission of an encrypted rolling code");
// the order/inversion lookup values match those published under MIT in
// zellyn/openers (secplus package) and were validated against over-the-wire
// captures. No GPL code was used or referenced.
//
// Pure logic, no ESP-IDF includes: unit tested on the host (test_secplus2.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "secplus1.h" // reuses secplus1_door_state_t for door state

#ifdef __cplusplus
extern "C"
{
#endif

#define SECPLUS2_WIRELINE_LEN 19
#define SECPLUS2_ROLLING_MAX ((1u << 28) - 1u)
#define SECPLUS2_FIXED_MAX ((1ULL << 40) - 1ULL)

// Default wall-device id we transmit as (opener accepts rolling-code wall
// frames without an explicit learn step).
#define SECPLUS2_CLIENT_ID_DEFAULT 0x539u

// ==== Command ids (12-bit, upper nibble in fixed, low byte in data) ====
#define SECPLUS2_CMD_GET_STATUS 0x080u
#define SECPLUS2_CMD_STATUS 0x081u
#define SECPLUS2_CMD_LOCK 0x18Cu
#define SECPLUS2_CMD_DOOR_ACTION 0x280u
#define SECPLUS2_CMD_LIGHT 0x281u
#define SECPLUS2_CMD_MOTOR_ON 0x284u
#define SECPLUS2_CMD_MOTION 0x285u

// Door action nibbles.
#define SECPLUS2_DOOR_CLOSE 0u
#define SECPLUS2_DOOR_OPEN 1u
#define SECPLUS2_DOOR_TOGGLE 2u
#define SECPLUS2_DOOR_STOP 3u

// Light / lock action nibbles.
#define SECPLUS2_OFF_UNLOCK 0u
#define SECPLUS2_ON_LOCK 1u
#define SECPLUS2_TOGGLE 2u

// Encode one wall-bus packet. rolling < 2^28, fixed < 2^40 (data is 32 bits).
// Returns 0 on success, -1 when an input is out of range.
int secplus2_encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data,
                             uint8_t packet[SECPLUS2_WIRELINE_LEN]);

// Decode one wall-bus packet. Rejects bad headers, illegal ternary pairs and
// parity errors. Returns 0 on success, -1 on any error.
int secplus2_decode_wireline(const uint8_t packet[SECPLUS2_WIRELINE_LEN],
                             uint32_t *rolling, uint64_t *fixed,
                             uint32_t *data);

// Command-level wrappers: split fixed into device id + 12-bit command and
// data into 20-bit payload (OPEN/CLOSE/LIGHT/... commands live here).
// Limits: device_id < 2^40, command < 2^12, payload < 2^20.
int secplus2_encode_command(uint32_t rolling, uint64_t device_id,
                            uint16_t command, uint32_t payload,
                            uint8_t packet[SECPLUS2_WIRELINE_LEN]);
int secplus2_decode_command(const uint8_t packet[SECPLUS2_WIRELINE_LEN],
                            uint32_t *rolling, uint64_t *device_id,
                            uint16_t *command, uint32_t *payload);

// ==== Link layer (framer, status decode, TX builders) ====

//RX framer: feed UART bytes, get 19-byte packets out. Hunts for 55 01 00,
//restarts on a re-sync mid-packet, drops partials idle >100 ms (a full
//packet takes ~20 ms at 9600 baud).
#define SECPLUS2_RX_IDLE_TIMEOUT_MS 100

typedef struct
{
    uint8_t buf[SECPLUS2_WIRELINE_LEN];
    uint8_t n;
    uint32_t last_ms;
} secplus2_framer_t;

void secplus2_framer_init(secplus2_framer_t *f);
// Feed one byte. Returns true when out is filled with a complete packet.
bool secplus2_framer_feed(secplus2_framer_t *f, uint8_t byte, uint32_t now_ms,
                          uint8_t out[SECPLUS2_WIRELINE_LEN]);
// Drop a stale partial packet (timeout only).
void secplus2_framer_expire(secplus2_framer_t *f, uint32_t now_ms);

// Decoded STATUS (0x081) reply.
typedef struct
{
    secplus1_door_state_t door;
    int8_t light; // 0 off, 1 on
    int8_t lock;  // 0 unlocked, 1 locked
    bool obstruction;
    bool learn;
} secplus2_status_t;

// Parse a STATUS reply from decoded command fields (nibble = data bits
// 8..11, byte1 = data bits 16..23, byte2 = data bits 24..31). False unless
// command is STATUS.
bool secplus2_parse_status(uint16_t command, uint8_t nibble, uint8_t byte1,
                           uint8_t byte2, secplus2_status_t *out);

// Map a STATUS door nibble (1..5) to a door state, UNKNOWN otherwise.
secplus1_door_state_t secplus2_door_from_nibble(uint8_t nibble);

// TX builder output: command id + 20-bit payload for secplus2_encode_command.
typedef struct
{
    uint16_t command;
    uint32_t payload;
} secplus2_tx_t;

// Door commands are two-phase: phase 0 sends (action,1,1) WITHOUT consuming
// a rolling code, phase 1 (+150 ms) sends (action,0,1) consuming one.
secplus2_tx_t secplus2_build_door(uint8_t action, int phase);
secplus2_tx_t secplus2_build_light(uint8_t action);
secplus2_tx_t secplus2_build_lock(uint8_t action);
secplus2_tx_t secplus2_build_get_status(void);

#ifdef __cplusplus
}
#endif
