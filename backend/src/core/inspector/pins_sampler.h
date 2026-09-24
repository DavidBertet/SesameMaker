// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Background edge counters for PIN_TYPE_DIGITAL pins: a 5 ms poll loop that
// records transitions the ~2 s UI poll would miss. Bus lines are excluded
// (ISR-captured while monitored, see bus_capture.h); analog is on-request
// (see pins_adc.h).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Start the sampler task (idempotent). The pin provider must be registered
// before the first request (see pin_inspector.h). Also drives the bus-capture
// deadline watchdog each loop so a session nobody closes still expires.
void pins_sampler_start(void);

// Snapshot the counters for one GPIO. Writes nothing when the pin was never
// sampled (or is out of range); idle_ms is only written with has_activity.
void pins_sampler_snapshot(int gpio, uint32_t now_ms, uint32_t *edges_out,
                           bool *has_activity_out, uint32_t *idle_ms_out);
