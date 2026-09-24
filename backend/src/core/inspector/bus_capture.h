// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// On-demand bus capture session: GPIO-ISR edge timestamps into a ring,
// framing analysis on poll (UART/I2C/SPI, see pin_inspector.h).
//
// Capture is on-demand only: the UI sends bus_capture_start while its tab is
// open (plus a heartbeat) and bus_capture_stop on close. A 60 s deadline
// stops a session nobody closes. Continuous ISR capture at bus rates would
// be wasteful, and polled sampling could never validate framing anyway.
//
// WHAT is watched comes from the app bus provider
// (pin_inspector_set_bus_provider) so this file never changes per project.

#pragma once

#include "pin_inspector.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Start (or heartbeat-refresh) the session: installs ISRs for the watched
// lines, extends the 60 s deadline. Keeps installed ISRs across heartbeats
// so a refresh costs no re-arm gap. No-op when no bus lines are declared.
void bus_capture_start(void);

// End the session immediately, removing the ISRs this session installed.
void bus_capture_stop(void);

bool bus_capture_active(void);

// Compact the bus provider table (drop unusable slots). Returns the count.
size_t bus_capture_table(bus_desc_t *out, size_t max);

// Fold the edges captured since the last poll into the session runs.
// No-op when no session is active. Runtime totals are indexed by bus-table
// position, so providers must return entries in stable order across calls;
// a changed table size restarts the totals.
void bus_capture_poll(const bus_desc_t *bdescs, size_t nb, uint32_t now_ms);

// Session runs, parallel to the last polled table (count = last table size).
const bus_run_t *bus_capture_runs(void);

// Deadline watchdog: stop a session nobody closes. Called from the sampler
// task (see pins_sampler.h) with the current time in ms.
void bus_capture_tick(uint32_t now_ms);
