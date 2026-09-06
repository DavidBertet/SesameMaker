// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Dry-contact (relay + reed limit sensor) protocol - pure logic only.
// No ESP-IDF includes here so it can be unit tested on the host, mirroring
// secplus1.c. Reference behavior: ratgdo dry_contact.cpp.
//
// Wiring model: a relay whose NO contacts sit across the opener's wall-button
// terminals (a pulse = one button press) plus 0, 1 or 2 reed switches:
//   open_limit  - closed while the door is fully open
//   close_limit - closed while the door is fully closed
// With no sensors the door is a blind toggle (state unknown). With only the
// close reed the door is closed vs not-closed. With both, intermediate
// motion is inferred from the last limit left behind.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    DRY_SENSORS_NONE = 0, // relay only, blind toggle
    DRY_SENSORS_CLOSE_ONLY = 1, // close reed only: closed vs not-closed
    DRY_SENSORS_BOTH = 2,       // open + close reeds: full inference
} dry_sensor_mode_t;

// Reuse the secplus1 door vocabulary so the controller, WS, MQTT and UI
// never branch on protocol. OPENING/CLOSING here mean "inferred".
// (Forward declared as int-compatible enum values; include secplus1.h where
// the full type is needed. Values mirror secplus1_door_state_t.)
#define DRY_DOOR_UNKNOWN 0
#define DRY_DOOR_OPEN 1
#define DRY_DOOR_CLOSED 2
#define DRY_DOOR_OPENING 3
#define DRY_DOOR_CLOSING 4
#define DRY_DOOR_STOPPED 5

// Static snapshot inference from normalized (active-level applied) reed
// levels. last_open/last_close remember which limit was last seen hit and
// disambiguate the in-between position (ratgdo precedence).
// Returns one of the DRY_DOOR_* values.
int drycontact_infer_both(bool open_hit, bool close_hit, bool last_open,
                          bool last_close);

// Close-only snapshot: CLOSED when the reed is hit, otherwise UNKNOWN
// (not-closed covers open/opening/closing; motion is layered by time in the
// ESP driver via the travel timeout).
int drycontact_infer_close_only(bool close_hit);

// Decide whether a relay pulse is needed right now to reach `target`
// (DRY_DOOR_* goal mapped from the generic open/close/stop/toggle action).
// *done is set when the target is reached or abandoned. Mirrors the
// secplus1 pursue contract. A dry relay cannot sense mid-travel stops, so
// stop only pulses while visibly OPENING/CLOSING.
bool drycontact_pulse_needed(int current, int target, bool *done);

// Target mapping for drycontact_pulse_needed (same numbers as DRY_DOOR_*,
// plus toggle = -1 meaning "always pulse once").
#define DRY_TARGET_TOGGLE -1

// GPIO allowlist for relay/reed wiring on classic ESP32: 6-11 are the SPI
// flash bus (rerouting them hangs flash access and boot-loops the device),
// 34-39 are input-only with no internal pullup (relay can't drive them,
// reeds need the pullup). Pure so the WS validation is host-testable.
bool drycontact_gpio_allowed(int8_t gpio);

// Switch debounce: feed the raw (electrical) level each poll; *stable holds
// the debounced level, *last_raw and *last_change_ms are bookkeeping (init
// last_change_ms to now_ms, *last_raw to the first sample). Returns true
// when *stable changed on this call.
bool drycontact_debounce(bool raw, uint32_t now_ms, bool *stable,
                         bool *last_raw, uint32_t *last_change_ms,
                         uint32_t debounce_ms);

#ifdef __cplusplus
}
#endif
