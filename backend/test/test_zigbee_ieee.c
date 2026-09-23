// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the EUI-64 / MAC-48 formatters.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/core/zigbee_ieee.h"

static int tests_run = 0;

static void test_eui64_format(void)
{
    tests_run++;
    const uint8_t eui[8] = {0x84, 0xFD, 0x27, 0xFF, 0xFE, 0x12, 0x34, 0x56};
    char out[24];
    zigbee_format_eui64(eui, out);
    assert(strcmp(out, "84:FD:27:FF:FE:12:34:56") == 0);
}

static void test_eui64_zeros(void)
{
    tests_run++;
    const uint8_t eui[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    char out[24];
    zigbee_format_eui64(eui, out);
    assert(strcmp(out, "00:00:00:00:00:00:00:00") == 0);
}

static void test_mac48_format(void)
{
    tests_run++;
    const uint8_t mac[6] = {0x08, 0x3a, 0xf2, 0xa1, 0xb2, 0xc3};
    char out[18];
    zigbee_format_mac48(mac, out);
    assert(strcmp(out, "08:3A:F2:A1:B2:C3") == 0);
}

int main(void)
{
    test_eui64_format();
    test_eui64_zeros();
    test_mac48_format();
    printf("test_zigbee_ieee: %d test groups passed\n", tests_run);
    return 0;
}
