// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Pure EUI-64 formatter for the Zigbee IEEE (long) address.
// Host-testable: no ESP-IDF headers, same style as zigbee_lqi.h.

#pragma once

#include <stdint.h>

// Format 8-byte EUI-64 as "XX:XX:XX:XX:XX:XX:XX:XX" (uppercase hex).
// out must hold at least 24 bytes (23 chars + NUL).
static inline void zigbee_format_eui64(const uint8_t eui[8], char out[24])
{
    static const char HEX[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++)
    {
        out[i * 3] = HEX[(eui[i] >> 4) & 0xF];
        out[i * 3 + 1] = HEX[eui[i] & 0xF];
        out[i * 3 + 2] = (i == 7) ? '\0' : ':';
    }
}

// Format 6-byte MAC as "XX:XX:XX:XX:XX:XX". out must hold >= 18 bytes.
static inline void zigbee_format_mac48(const uint8_t mac[6], char out[18])
{
    static const char HEX[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++)
    {
        out[i * 3] = HEX[(mac[i] >> 4) & 0xF];
        out[i * 3 + 1] = HEX[mac[i] & 0xF];
        out[i * 3 + 2] = (i == 5) ? '\0' : ':';
    }
}
