// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Rejoin-retry policy after a failed DEVICE_REBOOT (stack knows its network
// but the rejoin did not complete - parent unreachable, radio contention at
// boot, coordinator still starting). Pure logic, no ESP-IDF includes so it
// is unit tested on the host like zigbee_press.h.
//
// Two phases, attempt count is 0-based:
//   attempts 0..19  -> retry every 5s   (fast, covers transient contention)
//   attempts 20+    -> retry every 30s, forever (slow, covers coordinator or
//                      parent outage; a marginal link may need many tries -
//                      giving up strands the device until a manual reboot).
//
// Channel-agnostic by design: no channel mask is forced anywhere, so other
// deployments on any channel keep working.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ZIGBEE_REJOIN_FAST_MS 5000
#define ZIGBEE_REJOIN_FAST_N 20
#define ZIGBEE_REJOIN_SLOW_MS 30000

    // Delay before the next INITIALIZATION retry. Never gives up: after the
    // fast phase every attempt waits ZIGBEE_REJOIN_SLOW_MS.
    static inline int32_t zigbee_rejoin_delay_ms(uint32_t attempt)
    {
        if (attempt < ZIGBEE_REJOIN_FAST_N)
            return ZIGBEE_REJOIN_FAST_MS;
        return ZIGBEE_REJOIN_SLOW_MS;
    }

#ifdef __cplusplus
}
#endif
