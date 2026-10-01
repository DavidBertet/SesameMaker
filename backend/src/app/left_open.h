// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Left-open escalation, pure logic (no ESP-IDF includes here so it stays
// host-testable, mirroring drycontact.h). The watchdog task, NVS backing
// and WS endpoints live in ws_left_open.c; this file is the config shape,
// validation, and the transition machine.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEFT_OPEN_WEBHOOK_MAX 127
#define LEFT_OPEN_DURATION_MAX_S 86400 // 24 h sanity cap

typedef struct
{
    bool enabled;                  // master switch; off = everything inert
    uint32_t warn_s;               // warn after open this long (0 = never)
    uint32_t close_s;              // auto-close after open this long (0 = never)
    bool blink_light;              // flash the opener light 10 s before auto-close
                                   // (Security+ 1.0/2.0 only — dry-contact has no light)
    char webhook[LEFT_OPEN_WEBHOOK_MAX + 1]; // optional event POST URL ("" = none)
} left_open_cfg_t;

void left_open_defaults(left_open_cfg_t *cfg);
// False on out-of-range durations or oversize webhook. enabled is free —
// except: with warn armed, a webhook is mandatory (browser tab notifications
// are best-effort and must never be the warning path). Auto-close alone
// stays webhook-optional.
bool left_open_valid(const left_open_cfg_t *cfg);

// Watchdog state. open_since_ms == 0 means "not currently open".
typedef struct
{
    bool open;
    uint32_t open_since_ms;
    bool warned;
    bool blocked;
    uint32_t blocked_since_ms;
    bool blinking;            // in the 10 s light-warning window
    uint32_t blink_since_ms;
    bool light_was_on;        // opener light state when the blink started
} left_open_state_t;

#define LEFT_OPEN_NONE 0
#define LEFT_OPEN_WARN (1 << 0)    // notify now
#define LEFT_OPEN_BLINK (1 << 1)   // keep blinking (task toggles the light)
#define LEFT_OPEN_CLOSE (1 << 2)   // command a close now
#define LEFT_OPEN_BLOCKED (1 << 3) // wanted to close, obstruction in the way

// One poll step. now_ms is monotonic ms (wraps safely via u32 arithmetic).
// Semantics:
// - Disabled or not open: state cleared, NONE (closing the door resets all).
// - Fresh open (incl. boot with the door already open): timer starts now.
// - WARN once per open spell when warn_s elapses (skipped when warn_s == 0).
// - CLOSE when close_s elapses and the path is clear; the window restarts
//   so a stuck door re-attempts every close_s (warned stays set, no re-warn).
//   A close threshold below warn still fires WARN alongside (user must know).
// - With blink_light, CLOSE is preceded by a 10 s BLINK window (task toggles
//   the opener light; light_was_on restores it after). Obstruction any time
//   in the window aborts to BLOCKED and the next attempt blinks again.
// - Obstructed at close time: BLOCKED once, then re-fired every 60 s while
//   the close remains due (keeps nagging without spamming).
// NOTE: the NVS blob is the raw struct: growing it resets saved configs to
// defaults once (acceptable pre-release; version the key if it recurs).
// light_on is the opener light state (for light_was_on restore).
uint8_t left_open_step(left_open_state_t *st, uint32_t now_ms, bool door_open,
                       bool obstructed, bool light_on, bool light_capable,
                       const left_open_cfg_t *cfg);
