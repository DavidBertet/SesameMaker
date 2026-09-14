// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// BDB commissioning status code -> short name, for signal-handler logs.
// The stack delivers these in the signal payload (NO_NETWORK = scan
// heard no usable parent, TCLK_EX_FAILURE = trust-center key exchange
// failed). NULL when the code is not a BDB status — callers must say so
// instead of printing a bogus name.
//
// Numeric on purpose so host tests need no ESP-IDF headers (same pattern
// as zigbee_lqi.h); zigbee_stack.c pins each value to the matching
// EZB_BDB_STATUS_* constant with _Static_assert.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Short BDB commissioning status name, or NULL when the code is not a
// BDB status (no sub-cause to decode — callers report the raw code).
static inline const char *zigbee_bdb_status_name(uint8_t status)
{
    switch (status)
    {
    case 0:
        return "SUCCESS";
    case 1:
        return "IN_PROGRESS";
    case 2:
        return "NOT_AA_CAPABLE";
    case 3:
        return "NO_NETWORK";
    case 4:
        return "TARGET_FAILURE";
    case 5:
        return "FORMATION_FAILURE";
    case 6:
        return "NO_IDENTIFY_QUERY_RESPONSE";
    case 7:
        return "BINDING_TABLE_FULL";
    case 8:
        return "NO_SCAN_RESPONSE";
    case 9:
        return "NOT_PERMITTED";
    case 10:
        return "TCLK_EX_FAILURE";
    case 11:
        return "NOT_ON_A_NETWORK";
    case 12:
        return "ON_A_NETWORK";
    case 13:
        return "CANCELLED";
    case 14:
        return "DEV_ANNCE_SEND_FAILURE";
    default:
        return NULL;
    }
}

#ifdef __cplusplus
}
#endif
