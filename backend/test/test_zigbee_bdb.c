// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the BDB status-code names (log readability).

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/zigbee_bdb.h"

static int tests_run = 0;

static void test_known_statuses(void)
{
    tests_run++;
    assert(strcmp(zigbee_bdb_status_name(0), "SUCCESS") == 0);
    assert(strcmp(zigbee_bdb_status_name(3), "NO_NETWORK") == 0);
    assert(strcmp(zigbee_bdb_status_name(10), "TCLK_EX_FAILURE") == 0);
    assert(strcmp(zigbee_bdb_status_name(14), "DEV_ANNCE_SEND_FAILURE") == 0);
}

static void test_unknown_returns_null(void)
{
    tests_run++;
    // 0xffffffff (generic ZBOSS rejoin failure on 1.x) truncates to 0xff:
    // must be NULL, never a bogus name.
    assert(zigbee_bdb_status_name(15) == NULL);
    assert(zigbee_bdb_status_name(255) == NULL);
    assert(zigbee_bdb_status_name((uint8_t)0xffffffff) == NULL);
}

int main(void)
{
    test_known_statuses();
    test_unknown_returns_null();
    printf("test_zigbee_bdb: %d test groups passed\n", tests_run);
    return 0;
}
