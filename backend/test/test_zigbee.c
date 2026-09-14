// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the BOOT-button press classifier.

#include <assert.h>
#include <stdio.h>

#include "../src/app/zigbee_press.h"

static int tests_run = 0;

static void test_short_window(void)
{
    tests_run++;
    assert(zigbee_classify_press(0) == ZIGBEE_PRESS_IGNORE);
    assert(zigbee_classify_press(299) == ZIGBEE_PRESS_IGNORE);
    assert(zigbee_classify_press(300) == ZIGBEE_PRESS_SHORT);
    assert(zigbee_classify_press(1500) == ZIGBEE_PRESS_SHORT);
    assert(zigbee_classify_press(3000) == ZIGBEE_PRESS_SHORT);
}

static void test_dead_zone(void)
{
    tests_run++;
    // Between short-max and long-min: no action (accidental-wipe guard).
    assert(zigbee_classify_press(3001) == ZIGBEE_PRESS_IGNORE);
    assert(zigbee_classify_press(5000) == ZIGBEE_PRESS_IGNORE);
    assert(zigbee_classify_press(9999) == ZIGBEE_PRESS_IGNORE);
}

static void test_long(void)
{
    tests_run++;
    assert(zigbee_classify_press(10000) == ZIGBEE_PRESS_LONG);
    assert(zigbee_classify_press(30000) == ZIGBEE_PRESS_LONG);
}

int main(void)
{
    test_short_window();
    test_dead_zone();
    test_long();
    printf("test_zigbee: %d test groups passed\n", tests_run);
    return 0;
}
