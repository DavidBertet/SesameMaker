// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the generic Zigbee attribute vocabulary
// (zb_transport.h is IDF-free: stdint/stddef/bool only).

#include <assert.h>
#include <stdio.h>

#include "../src/core/zb_transport.h"

static int tests_run = 0;

static void test_type_sizes(void)
{
    tests_run++;
    assert(zb_attr_type_size(ZB_ATTR_BOOL) == 1);
    assert(zb_attr_type_size(ZB_ATTR_U8) == 1);
    assert(zb_attr_type_size(ZB_ATTR_S8) == 1);
    assert(zb_attr_type_size(ZB_ATTR_U16) == 2);
    assert(zb_attr_type_size(ZB_ATTR_S16) == 2);
    assert(zb_attr_type_size(ZB_ATTR_U32) == 4);
    assert(zb_attr_type_size(ZB_ATTR_S32) == 4);
    assert(zb_attr_type_size(ZB_ATTR_FLOAT) == 4);
    assert(zb_attr_type_size(ZB_ATTR_UNKNOWN) == 0);
}

static void test_report_widths_fit_transport_slots(void)
{
    tests_run++;
    // Transport latch slots hold 4 bytes: every fixed type must fit.
    assert(zb_attr_type_size(ZB_ATTR_BOOL) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_U8) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_S8) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_U16) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_S16) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_U32) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_S32) <= 4);
    assert(zb_attr_type_size(ZB_ATTR_FLOAT) <= 4);
}

int main(void)
{
    test_type_sizes();
    test_report_widths_fit_transport_slots();
    printf("test_zb_attr: %d test groups passed\n", tests_run);
    return 0;
}
