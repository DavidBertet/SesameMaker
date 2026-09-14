// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the dry-contact pure logic.

#include <assert.h>
#include <stdio.h>

#include "../src/app/drycontact.h"

static int tests_run = 0;

static void test_infer_both_settled(void)
{
    tests_run++;
    assert(drycontact_infer_both(true, false, true, false) == DRY_DOOR_OPEN);
    assert(drycontact_infer_both(false, true, false, true) == DRY_DOOR_CLOSED);
    // Both hit = wiring fault: never guess.
    assert(drycontact_infer_both(true, true, true, true) == DRY_DOOR_UNKNOWN);
}

static void test_infer_both_motion(void)
{
    tests_run++;
    // Just left the closed end -> opening; just left the open end -> closing.
    assert(drycontact_infer_both(false, false, false, true) == DRY_DOOR_OPENING);
    assert(drycontact_infer_both(false, false, true, false) == DRY_DOOR_CLOSING);
    // No history (boot mid-travel) -> unknown, not a guess.
    assert(drycontact_infer_both(false, false, false, false) == DRY_DOOR_UNKNOWN);
    assert(drycontact_infer_both(false, false, true, true) == DRY_DOOR_UNKNOWN);
}

static void test_infer_close_only(void)
{
    tests_run++;
    assert(drycontact_infer_close_only(true) == DRY_DOOR_CLOSED);
    // Not-closed covers open/opening/closing: report unknown honestly.
    assert(drycontact_infer_close_only(false) == DRY_DOOR_UNKNOWN);
}

static void test_pulse_needed(void)
{
    tests_run++;
    bool done = false;
    // Toggle always fires once.
    assert(drycontact_pulse_needed(DRY_DOOR_CLOSED, DRY_TARGET_TOGGLE, &done) && done);
    // Open reached / on the way: no pulse.
    assert(!drycontact_pulse_needed(DRY_DOOR_OPEN, DRY_DOOR_OPEN, &done) && done);
    assert(!drycontact_pulse_needed(DRY_DOOR_OPENING, DRY_DOOR_OPEN, &done) && !done);
    assert(drycontact_pulse_needed(DRY_DOOR_CLOSED, DRY_DOOR_OPEN, &done) && !done);
    assert(drycontact_pulse_needed(DRY_DOOR_UNKNOWN, DRY_DOOR_OPEN, &done) && !done);
    // Close mirrors.
    assert(!drycontact_pulse_needed(DRY_DOOR_CLOSED, DRY_DOOR_CLOSED, &done) && done);
    assert(!drycontact_pulse_needed(DRY_DOOR_CLOSING, DRY_DOOR_CLOSED, &done) && !done);
    assert(drycontact_pulse_needed(DRY_DOOR_OPEN, DRY_DOOR_CLOSED, &done) && !done);
    // Stop only pulses while visibly moving; blind/settled = no-op.
    assert(drycontact_pulse_needed(DRY_DOOR_OPENING, DRY_DOOR_STOPPED, &done) && done);
    assert(!drycontact_pulse_needed(DRY_DOOR_UNKNOWN, DRY_DOOR_STOPPED, &done) && done);
    assert(!drycontact_pulse_needed(DRY_DOOR_OPEN, DRY_DOOR_STOPPED, &done) && done);
    assert(!drycontact_pulse_needed(DRY_DOOR_CLOSED, DRY_DOOR_STOPPED, &done) && done);
}

static void test_gpio_allowlist(void)
{
    tests_run++;
    // Classic ESP32: 6-11 are the SPI flash bus, 34-39 input-only/no pullup.
    assert(drycontact_gpio_allowed(5));
    assert(drycontact_gpio_allowed(17));
    assert(drycontact_gpio_allowed(18));
    assert(!drycontact_gpio_allowed(6));
    assert(!drycontact_gpio_allowed(7));
    assert(!drycontact_gpio_allowed(11));
    assert(!drycontact_gpio_allowed(34));
    assert(!drycontact_gpio_allowed(39));
    assert(!drycontact_gpio_allowed(-1));
}

static void test_debounce(void)
{
    tests_run++;
    bool stable = false, last_raw = false;
    uint32_t changed_at = 1000;
    // Bounce under the window never flips the stable level.
    assert(!drycontact_debounce(true, 1010, &stable, &last_raw, &changed_at, 200));
    assert(!stable);
    assert(!drycontact_debounce(false, 1020, &stable, &last_raw, &changed_at, 200));
    assert(!stable);
    // Held past the window flips exactly once.
    assert(!drycontact_debounce(true, 1030, &stable, &last_raw, &changed_at, 200));
    assert(drycontact_debounce(true, 1231, &stable, &last_raw, &changed_at, 200));
    assert(stable);
    assert(!drycontact_debounce(true, 1500, &stable, &last_raw, &changed_at, 200));
    // NULL bookkeeping never crashes.
    assert(!drycontact_debounce(true, 1500, NULL, NULL, NULL, 200));
}

int main(void)
{
    test_infer_both_settled();
    test_infer_both_motion();
    test_infer_close_only();
    test_pulse_needed();
    test_gpio_allowlist();
    test_debounce();
    printf("test_drycontact: %d passed\n", tests_run);
    return 0;
}
