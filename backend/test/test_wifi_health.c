// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the STA link-health watchdog decision logic.

#include "../src/core/wifi_health.h"

#include <assert.h>
#include <stdio.h>

static int tests_run = 0;

static void test_ok_means_no_action(void)
{
    tests_run++;
    assert(wifi_health_check(WIFI_HEALTH_ERR_OK) == WIFI_HEALTH_ACTION_NONE);
}

static void test_zombie_link_reassociates(void)
{
    tests_run++;
    assert(wifi_health_check(WIFI_HEALTH_ERR_NOT_ASSOC) == WIFI_HEALTH_ACTION_REASSOC);
}

static void test_transient_errors_do_not_reassociate(void)
{
    tests_run++;
    // Busy/not-started/unknown: re-associating mid-flight could fight a
    // genuine connect attempt already running — just observe.
    assert(wifi_health_check(WIFI_HEALTH_ERR_TRANSIENT) == WIFI_HEALTH_ACTION_NONE);
}

static void test_weak_rssi_threshold(void)
{
    tests_run++;
    assert(wifi_health_is_weak_rssi(-86) == 1); // the field case
    assert(wifi_health_is_weak_rssi(-75) == 1); // at threshold
    assert(wifi_health_is_weak_rssi(-74) == 0); // still fine
    assert(wifi_health_is_weak_rssi(-60) == 0);
}

int main(void)
{
    test_ok_means_no_action();
    test_zombie_link_reassociates();
    test_transient_errors_do_not_reassociate();
    test_weak_rssi_threshold();
    printf("test_wifi_health: %d tests passed\n", tests_run);
    return 0;
}
