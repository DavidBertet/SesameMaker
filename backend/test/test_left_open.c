// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the left-open escalation machine.

#include "../src/app/left_open.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;

static void test_defaults_and_validation(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    left_open_defaults(&cfg);
    assert(!cfg.enabled && cfg.warn_s == 600 && cfg.close_s == 0);
    assert(left_open_valid(&cfg));
    cfg.warn_s = LEFT_OPEN_DURATION_MAX_S + 1;
    assert(!left_open_valid(&cfg));
    cfg.warn_s = 60;
    memset(cfg.webhook, 'x', sizeof(cfg.webhook));
    assert(!left_open_valid(&cfg));
    cfg.webhook[LEFT_OPEN_WEBHOOK_MAX] = '\0';
    assert(left_open_valid(&cfg));
}

static void cfg_on(uint32_t warn_s, uint32_t close_s, left_open_cfg_t *cfg)
{
    left_open_defaults(cfg);
    cfg->enabled = true;
    cfg->warn_s = warn_s;
    cfg->close_s = close_s;
}

static void test_webhook_required_when_armed(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    // Disabled: no webhook needed.
    left_open_defaults(&cfg);
    assert(left_open_valid(&cfg));
    // Warn armed: webhook mandatory.
    cfg_on(60, 0, &cfg);
    assert(!left_open_valid(&cfg));
    snprintf(cfg.webhook, sizeof(cfg.webhook), "https://ntfy.sh/topic");
    assert(left_open_valid(&cfg));
    // Close armed without warn: optional.
    cfg_on(0, 60, &cfg);
    assert(left_open_valid(&cfg));
}

static void test_closed_and_disabled_are_inert(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(60, 120, &cfg);
    left_open_state_t st = {0};
    assert(left_open_step(&st, 1000, false, false, &cfg) == LEFT_OPEN_NONE);
    cfg.enabled = false;
    assert(left_open_step(&st, 1000, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(!st.open);
}

static void test_warn_once_then_close_restarts_window(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(60, 120, &cfg);
    left_open_state_t st = {0};
    assert(left_open_step(&st, 0, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(left_open_step(&st, 59000, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(left_open_step(&st, 60000, true, false, &cfg) == LEFT_OPEN_WARN);
    assert(left_open_step(&st, 61000, true, false, &cfg) == LEFT_OPEN_NONE); // no re-warn
    assert(left_open_step(&st, 119999, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(left_open_step(&st, 120000, true, false, &cfg) == LEFT_OPEN_CLOSE);
    // Window restarted: next close due another close_s later, no re-warn.
    assert(left_open_step(&st, 180000, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(left_open_step(&st, 240000, true, false, &cfg) == LEFT_OPEN_CLOSE);
    // Closing the door resets everything.
    assert(left_open_step(&st, 241000, false, false, &cfg) == LEFT_OPEN_NONE);
    assert(!st.open && !st.warned);
}

static void test_close_below_warn_fires_both(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(300, 60, &cfg);
    left_open_state_t st = {0};
    left_open_step(&st, 0, true, false, &cfg);
    assert(left_open_step(&st, 60000, true, false, &cfg) == (LEFT_OPEN_WARN | LEFT_OPEN_CLOSE));
}

static void test_obstructed_blocks_and_retries(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(60, 120, &cfg);
    left_open_state_t st = {0};
    left_open_step(&st, 0, true, true, &cfg);
    left_open_step(&st, 60000, true, true, &cfg); // warn still fires
    assert(left_open_step(&st, 120000, true, true, &cfg) == LEFT_OPEN_BLOCKED);
    assert(left_open_step(&st, 150000, true, true, &cfg) == LEFT_OPEN_NONE); // throttled
    assert(left_open_step(&st, 180000, true, true, &cfg) == LEFT_OPEN_BLOCKED); // retry
    // Cleared path closes immediately (due long past).
    assert(left_open_step(&st, 181000, true, false, &cfg) == LEFT_OPEN_CLOSE);
}

static void test_zero_durations_disable_each(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(0, 0, &cfg);
    left_open_state_t st = {0};
    left_open_step(&st, 0, true, false, &cfg);
    assert(left_open_step(&st, 1000000, true, false, &cfg) == LEFT_OPEN_NONE);
    cfg_on(0, 60, &cfg);
    memset(&st, 0, sizeof(st));
    left_open_step(&st, 0, true, false, &cfg);
    // Close with warn disabled still warns alongside (user must know).
    assert(left_open_step(&st, 60000, true, false, &cfg) == (LEFT_OPEN_WARN | LEFT_OPEN_CLOSE));
}

static void test_wrap_safe(void)
{
    tests_run++;
    left_open_cfg_t cfg;
    cfg_on(60, 0, &cfg);
    left_open_state_t st = {0};
    left_open_step(&st, 0xFFFFFF00u, true, false, &cfg);
    assert(left_open_step(&st, 0xFFFFFF00u + 59999u, true, false, &cfg) == LEFT_OPEN_NONE);
    assert(left_open_step(&st, 0xFFFFFF00u + 60000u, true, false, &cfg) == LEFT_OPEN_WARN);
}

int main(void)
{
    test_defaults_and_validation();
    test_webhook_required_when_armed();
    test_closed_and_disabled_are_inert();
    test_warn_once_then_close_restarts_window();
    test_close_below_warn_fires_both();
    test_obstructed_blocks_and_retries();
    test_zero_durations_disable_each();
    test_wrap_safe();
    printf("test_left_open: %d tests passed\n", tests_run);
    return 0;
}
