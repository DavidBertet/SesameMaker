// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the fallback-AP auto-off sign convention.

#include "../src/core/ap_window.h"

#include <assert.h>
#include <stdio.h>

static int tests_run = 0;

static void test_zero_disables_ap(void)
{
    tests_run++;
    assert(ap_window_action(0) == AP_WINDOW_DISABLED);
}

static void test_negative_keeps_ap_forever(void)
{
    tests_run++;
    assert(ap_window_action(-1) == AP_WINDOW_FOREVER);
    assert(ap_window_action(-999) == AP_WINDOW_FOREVER);
}

static void test_positive_arms_timed_window(void)
{
    tests_run++;
    assert(ap_window_action(1) == AP_WINDOW_TIMED);
    assert(ap_window_action(120) == AP_WINDOW_TIMED);
}

int main(void)
{
    test_zero_disables_ap();
    test_negative_keeps_ap_forever();
    test_positive_arms_timed_window();
    printf("test_ap_window: %d tests passed\n", tests_run);
    return 0;
}
