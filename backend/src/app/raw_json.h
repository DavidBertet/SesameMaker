// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Pure JSON serialization for the raw bus-traffic dump. No ESP-IDF
// dependencies, so it stays host-testable.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t timestamp_ms;
    uint8_t byte;
} raw_json_event_t;

// Build `{"type":"garage_raw","rx":[...],"tx":[...],"rx_total":N,"tx_total":M}`
// from the RX/TX ring logs (oldest first; entries with timestamp 0 skipped).
// Two-call API: pass buf=NULL size=0 for the required bytes (excluding NUL),
// then call again with a buffer of that size + 1. When buf is non-NULL the
// result is NUL-terminated and never writes past buf[size-1]. Returns the
// logical length excluding the trailing NUL.
size_t raw_json_garage_payload(char *buf, size_t size,
                               const raw_json_event_t *rx, size_t rx_len,
                               uint32_t rx_head, uint32_t rx_total,
                               const raw_json_event_t *tx, size_t tx_len,
                               uint32_t tx_head, uint32_t tx_total);