// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// BOOT-button press classifier - pure logic, no ESP-IDF includes so it is
// unit tested on the host like secplus1/drycontact. The button is the BOOT
// strapping pin (GPIO9 on C6): only sampled at runtime, never driven, so
// reset/download-mode behavior is unaffected.
//
//   < 300ms            -> ignore (bounce/tap)
//   300ms..3000ms      -> short press  (open pairing window)
//   >= 10000ms         -> long press   (factory reset + leave network)
//   in between         -> ignore (no action, avoids accidental wipes)

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ZIGBEE_PRESS_SHORT_MS_MIN 300
#define ZIGBEE_PRESS_SHORT_MS_MAX 3000
#define ZIGBEE_PRESS_LONG_MS_MIN 10000

typedef enum
{
    ZIGBEE_PRESS_IGNORE = 0,
    ZIGBEE_PRESS_SHORT = 1, // open pairing window
    ZIGBEE_PRESS_LONG = 2,  // factory reset
} zigbee_press_t;

static inline zigbee_press_t zigbee_classify_press(uint32_t held_ms)
{
    if (held_ms >= ZIGBEE_PRESS_LONG_MS_MIN)
        return ZIGBEE_PRESS_LONG;
    if (held_ms >= ZIGBEE_PRESS_SHORT_MS_MIN &&
        held_ms <= ZIGBEE_PRESS_SHORT_MS_MAX)
        return ZIGBEE_PRESS_SHORT;
    return ZIGBEE_PRESS_IGNORE;
}

#ifdef __cplusplus
}
#endif
