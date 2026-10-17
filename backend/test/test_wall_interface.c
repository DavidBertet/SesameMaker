// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the dual-2N7000 wall-interface polarity.
// Both FET stages invert: TX pin LOW = bus HIGH idle, RX pin LOW = wall
// HIGH idle. The UART peripheral inversion handles the rest; the raw
// gpio_get_level guards must compare against GARAGE_RX_IDLE_LEVEL.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/core/constants.h"

static int tests_run = 0;

static void test_rx_idle_is_inverted(void)
{
    tests_run++;
    // RX FET inverts: wall HIGH idle reads LOW at the ESP pin.
    assert(GARAGE_RX_IDLE_LEVEL == 0);
    assert(GARAGE_RX_BUSY_LEVEL == 1);
    assert(GARAGE_RX_IDLE_LEVEL != GARAGE_RX_BUSY_LEVEL);
}

static void test_tx_idle_releases_bus(void)
{
    tests_run++;
    // TX FET off at pin LOW: bus released HIGH.
    assert(GARAGE_TX_IDLE_LEVEL == 0);
}

static void test_busy_guard_logic(void)
{
    tests_run++;
    // Mirrors tx_due()/send_tx(): TX proceeds only at IDLE, defers on BUSY.
    int idle_pin = GARAGE_RX_IDLE_LEVEL;
    int busy_pin = GARAGE_RX_BUSY_LEVEL;
    assert((idle_pin != GARAGE_RX_IDLE_LEVEL) == false);
    assert((busy_pin != GARAGE_RX_IDLE_LEVEL) == true);
    assert((idle_pin == GARAGE_RX_IDLE_LEVEL) == true);
    assert((busy_pin == GARAGE_RX_IDLE_LEVEL) == false);
}

int main(void)
{
    test_rx_idle_is_inverted();
    test_tx_idle_releases_bus();
    test_busy_guard_logic();
    printf("test_wall_interface: %d passed\n", tests_run);
    return 0;
}
