// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the Zigbee scan-channel configuration.

#include <assert.h>
#include <stdio.h>

#include "../src/core/channel_config.h"

static int tests_run = 0;

static void test_valid_channels(void)
{
    tests_run++;
    assert(zigbee_channel_cfg_valid(0));
    assert(zigbee_channel_cfg_valid(11));
    assert(zigbee_channel_cfg_valid(20));
    assert(zigbee_channel_cfg_valid(26));
    assert(!zigbee_channel_cfg_valid(1));
    assert(!zigbee_channel_cfg_valid(10));
    assert(!zigbee_channel_cfg_valid(27));
    assert(!zigbee_channel_cfg_valid(255));
}

static void test_auto_uses_all_channels(void)
{
    tests_run++;
    // 0 (auto) = full 11..26 sweep with the low scan duration.
    assert(zigbee_channel_mask(0) == ZB_CHANNEL_MASK_ALL);
    assert(zigbee_channel_mask(0) == 0x07FFF800u);
    assert(zigbee_channel_scan_duration(0) == ZB_SCAN_DURATION_AUTO);
    assert(zigbee_channel_scan_duration(0) == 4);
}

static void test_single_channel_mask(void)
{
    tests_run++;
    // Bit N: ch 11 -> 0x00000800, ch 26 -> 0x04000000.
    assert(zigbee_channel_mask(11) == 0x00000800u);
    assert(zigbee_channel_mask(26) == 0x04000000u);
    assert(zigbee_channel_mask(20) == (1u << 20));
    // Pinned channel = longer dwell per channel.
    assert(zigbee_channel_scan_duration(11) == ZB_SCAN_DURATION_FIXED);
    assert(zigbee_channel_scan_duration(26) == ZB_SCAN_DURATION_FIXED);
    assert(zigbee_channel_scan_duration(20) == 6);
}

static void test_invalid_falls_back_to_auto(void)
{
    tests_run++;
    // Garbage config values degrade to the safe all-channel sweep.
    assert(zigbee_channel_mask(1) == ZB_CHANNEL_MASK_ALL);
    assert(zigbee_channel_mask(255) == ZB_CHANNEL_MASK_ALL);
    assert(zigbee_channel_scan_duration(1) == ZB_SCAN_DURATION_AUTO);
}

int main(void)
{
    test_valid_channels();
    test_auto_uses_all_channels();
    test_single_channel_mask();
    test_invalid_falls_back_to_auto();
    printf("test_channel_config: %d test groups passed\n", tests_run);
    return 0;
}