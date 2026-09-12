// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the Zigbee rejoin-retry policy.

#include <assert.h>
#include <stdio.h>

#include "../src/zigbee_rejoin.h"

static int tests_run = 0;

static void test_fast_phase(void)
{
    tests_run++;
    for (uint32_t i = 0; i < ZIGBEE_REJOIN_FAST_N; i++)
        assert(zigbee_rejoin_delay_ms(i) == ZIGBEE_REJOIN_FAST_MS);
}

static void test_slow_phase(void)
{
    tests_run++;
    assert(zigbee_rejoin_delay_ms(ZIGBEE_REJOIN_FAST_N) == ZIGBEE_REJOIN_SLOW_MS);
    assert(zigbee_rejoin_delay_ms(ZIGBEE_REJOIN_FAST_N + 23) == ZIGBEE_REJOIN_SLOW_MS);
}

static void test_never_gives_up(void)
{
    tests_run++;
    assert(zigbee_rejoin_delay_ms(30) == ZIGBEE_REJOIN_SLOW_MS);
    assert(zigbee_rejoin_delay_ms(1000) == ZIGBEE_REJOIN_SLOW_MS);
    assert(zigbee_rejoin_delay_ms(1000000) == ZIGBEE_REJOIN_SLOW_MS);
}

int main(void)
{
    test_fast_phase();
    test_slow_phase();
    test_never_gives_up();
    printf("test_zigbee_rejoin: %d test groups passed\n", tests_run);
    return 0;
}
