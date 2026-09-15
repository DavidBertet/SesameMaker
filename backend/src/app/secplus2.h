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

#ifdef __cplusplus
extern "C"
{
#endif

#define SECPLUS2_WIRELINE_LEN 19
#define SECPLUS2_ROLLING_MAX ((1u << 28) - 1u)
#define SECPLUS2_FIXED_MAX ((1ULL << 40) - 1ULL)

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

#ifdef __cplusplus
}
#endif
