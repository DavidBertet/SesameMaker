// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 2.0 ESP driver: wall-device emulation on the 2-wire bus (9600
// 8N1, 19-byte packets, break pulse ahead of each TX). Owns the rolling-code
// counter (NVS "sp2_roll"), polls the UART, decodes opener packets and
// reports into the garage controller via its report setters; the controller
// owns state, broadcast and MQTT.
//
// UNTESTED ON HARDWARE: no secplus2 opener is available for validation.
// Break polarity/timing, collision behavior and the opener-accepts-unsolicited
// wall frames assumption all need a live yellow-learn-button opener.

#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t protocol_secplus2_init(void);
esp_err_t protocol_secplus2_start(void);

// Entering secplus2 (boot or set_protocol): switch the UART to 9600 8N1 and
// ask the opener for status.
void protocol_secplus2_activate(void);

// Door nibble: SECPLUS2_DOOR_* (toggle/open/close/stop). Sends the two-phase
// press (blocks ~200 ms), consumes one rolling code.
esp_err_t protocol_secplus2_door(uint8_t action);
// Light / lock nibble: 0 off/unlock, 1 on/lock, 2 toggle.
esp_err_t protocol_secplus2_light(uint8_t action);
esp_err_t protocol_secplus2_lock(uint8_t action);

// Immediate status query (used by garage_sync).
void protocol_secplus2_resync(void);
