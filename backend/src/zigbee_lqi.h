// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Parent-link helpers - pure logic, no ESP-IDF includes so it is unit
// tested on the host like zigbee_press.h / zigbee_rejoin.h.
//
// The stack polls our own neighbor table (Mgmt_Lqi_req to self) and picks
// the entry whose relationship is the parent. Relationship is passed as a
// plain number so hosts need no IDF headers: it must equal
// EZB_NWK_RELATIONSHIP_PARENT (0); zigbee_stack.c enforces that with a
// _Static_assert at build time.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Relationship value meaning "parent" (== EZB_NWK_RELATIONSHIP_PARENT).
#define ZB_LQI_PARENT_RELATIONSHIP 0
// Neighbor entries scanned per response page (ED tables are single-digit).
#define ZB_LQI_MAX_ENTRIES 16
// First poll delay after join; further polls are on demand
// (zigbee_stack_poll_lqi, e.g. on get_zigbee_config reads).
#define ZB_LQI_FIRST_MS 5000

// Index of the parent entry, or -1 when no parent is listed.
static inline int zigbee_parent_index(const uint8_t *relationships, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        if (relationships[i] == ZB_LQI_PARENT_RELATIONSHIP)
            return i;
    }
    return -1;
}

#ifdef __cplusplus
}
#endif
