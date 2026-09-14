// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the Zigbee parent-entry picker.

#include <assert.h>
#include <stdio.h>

#include "../src/core/zigbee_lqi.h"

static int tests_run = 0;

static void test_parent_first(void)
{
    tests_run++;
    const uint8_t rel[] = {0, 1, 3};
    assert(zigbee_parent_index(rel, 3) == 0);
}

static void test_parent_middle(void)
{
    tests_run++;
    const uint8_t rel[] = {1, 3, 0};
    assert(zigbee_parent_index(rel, 3) == 2);
}

static void test_no_parent(void)
{
    tests_run++;
    const uint8_t rel[] = {1, 2, 3};
    assert(zigbee_parent_index(rel, 3) == -1);
}

static void test_empty(void)
{
    tests_run++;
    assert(zigbee_parent_index(NULL, 0) == -1);
}

int main(void)
{
    test_parent_first();
    test_parent_middle();
    test_no_parent();
    test_empty();
    printf("test_zigbee_lqi: %d test groups passed\n", tests_run);
    return 0;
}
