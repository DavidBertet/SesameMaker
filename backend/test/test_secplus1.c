// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the pure secplus1 protocol logic.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/secplus1.h"

static int tests_run = 0;

static void test_decode_door_state(void)
{
    tests_run++;
    // resp & 0x7: 0/6 stopped, 1 opening, 2 open, 4 closing, 5 closed
    assert(secplus1_decode_door_state(0x00) == SECPLUS1_DOOR_STOPPED);
    assert(secplus1_decode_door_state(0x06) == SECPLUS1_DOOR_STOPPED);
    assert(secplus1_decode_door_state(0x01) == SECPLUS1_DOOR_OPENING);
    assert(secplus1_decode_door_state(0x02) == SECPLUS1_DOOR_OPEN);
    assert(secplus1_decode_door_state(0x04) == SECPLUS1_DOOR_CLOSING);
    assert(secplus1_decode_door_state(0x05) == SECPLUS1_DOOR_CLOSED);
    assert(secplus1_decode_door_state(0x03) == SECPLUS1_DOOR_UNKNOWN);
    // High bits are ignored.
    assert(secplus1_decode_door_state(0x84) == SECPLUS1_DOOR_CLOSING);
    // Real GDO reply bytes observed on the bus (high bits + parity tag ignored).
    assert(secplus1_decode_door_state(0x52) == SECPLUS1_DOOR_OPEN);   // normal open
    assert(secplus1_decode_door_state(0x55) == SECPLUS1_DOOR_CLOSED); // normal closed
    assert(secplus1_decode_door_state(0x51) == SECPLUS1_DOOR_OPENING); // user-reported opening
}

static void test_decode_other_status(void)
{
    tests_run++;
    bool light = false, locked = true;
    // light = (resp>>2)&1, locked = (~resp>>3)&1
    secplus1_decode_other_status(0x0C, &light, &locked); // light on, unlocked
    assert(light == true);
    assert(locked == false);

    secplus1_decode_other_status(0x08, &light, &locked); // light off, unlocked
    assert(light == false);
    assert(locked == false);

    secplus1_decode_other_status(0x00, &light, &locked); // light off, locked
    assert(light == false);
    assert(locked == true);
}

static void test_rx_parser_single_bytes(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    assert(secplus1_rx_parser_feed(&p, 0x30, 1000, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_TOGGLE_DOOR_PRESS);

    assert(secplus1_rx_parser_feed(&p, 0x37, 1100, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS_0X37);
}

static void test_rx_parser_two_byte_packets(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    // First byte of a status reply does not complete a command.
    assert(secplus1_rx_parser_feed(&p, 0x38, 2000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x05, 2010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(cmd.resp == 0x05);

    assert(secplus1_rx_parser_feed(&p, 0x3A, 3000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x04, 3010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_OTHER_STATUS);
}

static void test_rx_parser_invalid_byte_resyncs(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    // Garbage mid-stream resets framing, next panel byte still decodes.
    assert(secplus1_rx_parser_feed(&p, 0x55, 100, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_INVALID_BYTE);
    assert(secplus1_rx_parser_feed(&p, 0x30, 110, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_TOGGLE_DOOR_PRESS);
}

static void test_rx_parser_incomplete_packet_times_out(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    assert(secplus1_rx_parser_feed(&p, 0x38, 1000, &cmd) == false);
    // A lone 0 byte after >100ms discards the stale packet start...
    assert(secplus1_rx_parser_feed(&p, 0x00, 1200, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_INVALID_BYTE);
    // ...and a following command byte decodes normally.
    assert(secplus1_rx_parser_feed(&p, 0x32, 1300, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_TOGGLE_LIGHT_PRESS);
}

static void test_rx_parser_real_gdo_reply_bytes(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    // 0x38 echo + 0x51 reply → opening (user-reported value)
    assert(secplus1_rx_parser_feed(&p, 0x38, 1000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x51, 1010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(secplus1_decode_door_state(cmd.resp) == SECPLUS1_DOOR_OPENING);

    // 0x38 + 0x55 → closed
    assert(secplus1_rx_parser_feed(&p, 0x38, 2000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x55, 2010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(secplus1_decode_door_state(cmd.resp) == SECPLUS1_DOOR_CLOSED);

    // 0x38 + 0x00 → stopped
    assert(secplus1_rx_parser_feed(&p, 0x38, 3000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x00, 3010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(secplus1_decode_door_state(cmd.resp) == SECPLUS1_DOOR_STOPPED);

    // 0x39 + 0x00 → obstruction clear
    assert(secplus1_rx_parser_feed(&p, 0x39, 4000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x00, 4010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_OBSTRUCTION);
    assert(cmd.resp == 0x00);

    // 0x3A + reply → other status
    assert(secplus1_rx_parser_feed(&p, 0x3A, 5000, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x5C, 5010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_OTHER_STATUS);
}

static void test_rx_is_self_echo(void)
{
    tests_run++;
    // Same byte read back within the window: our own echo on the shared bus.
    assert(secplus1_rx_is_self_echo(0x38, 0x38, 10009, 10000, 60) == true);
    // Same byte but outside the window: real bus traffic, not our echo.
    assert(secplus1_rx_is_self_echo(0x38, 0x38, 10100, 10000, 60) == false);
    // Different byte within the window (the opener's reply): not our echo.
    assert(secplus1_rx_is_self_echo(0x52, 0x38, 10020, 10000, 60) == false);
    // Edge: exactly at the window boundary still counts as our echo.
    assert(secplus1_rx_is_self_echo(0x3A, 0x3A, 10060, 10000, 60) == true);
    // Nothing sent yet in the session: a matching byte is not an echo.
    assert(secplus1_rx_is_self_echo(0x38, 0x38, 500, 0, 60) == false);
}

static void test_rx_parser_prime_pairs_own_query_with_reply(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    // We transmit 0x38 (echo flushed): priming pairs the opener's lone
    // reply byte into a complete door status packet.
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_DOOR_STATUS, 1000) == true);
    assert(secplus1_rx_parser_feed(&p, 0x55, 1020, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(cmd.resp == 0x55);
    assert(secplus1_decode_door_state(cmd.resp) == SECPLUS1_DOOR_CLOSED);

    // Obstruction and other status queries prime too.
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_OBSTRUCTION, 2000) == true);
    assert(secplus1_rx_parser_feed(&p, 0x00, 2010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_OBSTRUCTION);

    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_OTHER_STATUS, 3000) == true);
    assert(secplus1_rx_parser_feed(&p, 0x51, 3010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_OTHER_STATUS);
    assert(cmd.resp == 0x51);

    // Non-query TX bytes do not prime; the parser stays idle.
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_TOGGLE_DOOR_PRESS, 4000) == false);
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_DOOR_STATUS_0X37, 4000) == false);
    assert(secplus1_rx_parser_feed(&p, 0x30, 4010, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_TOGGLE_DOOR_PRESS);
}

static void test_rx_parser_expire_drops_stale_half_packets(void)
{
    tests_run++;
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    // Primed query with no reply within the timeout: expire drops it, so the
    // orphaned reply byte (arriving late) is ignored instead of mispaired.
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_DOOR_STATUS, 1000) == true);
    secplus1_rx_parser_expire(&p, 1200);
    assert(secplus1_rx_parser_feed(&p, 0x55, 1260, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_INVALID_BYTE);

    // Within the timeout window expire is a no-op and the reply still pairs.
    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_OTHER_STATUS, 2000) == true);
    secplus1_rx_parser_expire(&p, 2050);
    assert(secplus1_rx_parser_feed(&p, 0x5C, 2060, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_OTHER_STATUS);

    // Expiring an idle parser is a no-op.
    secplus1_rx_parser_expire(&p, 3000);
    assert(secplus1_rx_parser_feed(&p, 0x3A, 3100, &cmd) == false);
    assert(secplus1_rx_parser_feed(&p, 0x04, 3110, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_QUERY_OTHER_STATUS);
}

static void test_parser_echo_pairs_with_prime_and_poisons_door(void)
{
    tests_run++;
    // Regression documentation: the parser cannot tell our own echo from the
    // opener's reply. If an echo of our 0x38 transmission reaches the primed
    // parser, resp decodes as door "stopped" (0x38 & 0x7 == 0) while the
    // real reply says e.g. "open" (0x52 & 0x7 == 2). Those two values
    // alternate and keep resetting the controller's 2-sample door debounce,
    // freezing the door state. The controller must drop echoes (via
    // secplus1_rx_is_self_echo) before feeding the parser. This test pins
    // the hazardous decoder output so the fix cannot silently regress.
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;

    assert(secplus1_rx_parser_prime(&p, SECPLUS1_CMD_QUERY_DOOR_STATUS, 1000) == true);
    assert(secplus1_rx_parser_feed(&p, 0x38, 1010, &cmd) == true); // our echo
    assert(cmd.type == SECPLUS1_RX_QUERY_DOOR_STATUS);
    assert(cmd.resp == 0x38);
    assert(secplus1_decode_door_state(cmd.resp) == SECPLUS1_DOOR_STOPPED);
}

static void test_rx_parser_valid_range_guard(void)
{
    tests_run++;
    // Bytes outside 0x30..0x3A are treated as invalid and resync the parser,
    // so stray replies / noise can't silently corrupt a multi-byte packet.
    secplus1_rx_parser_t p;
    secplus1_rx_parser_init(&p);
    secplus1_rx_cmd_t cmd;
    assert(secplus1_rx_parser_feed(&p, 0x53, 1000, &cmd) == true);
    assert(cmd.type == SECPLUS1_RX_INVALID_BYTE);
}

static void test_pursue_open(void)
{
    tests_run++;
    bool done;
    // From closed: one toggle, then wait while opening, done when open.
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_CLOSED, SECPLUS1_TARGET_OPEN, &done) == true);
    assert(done == false);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_OPENING, SECPLUS1_TARGET_OPEN, &done) == false);
    assert(done == false);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_OPEN, SECPLUS1_TARGET_OPEN, &done) == false);
    assert(done == true);
    // From stopped the first toggle starts closing (ratgdo observed
    // behavior); when closing is confirmed we toggle again.
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_STOPPED, SECPLUS1_TARGET_OPEN, &done) == true);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_CLOSING, SECPLUS1_TARGET_OPEN, &done) == true);
}

static void test_pursue_close_and_stop(void)
{
    tests_run++;
    bool done;
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_OPEN, SECPLUS1_TARGET_CLOSE, &done) == true);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_CLOSING, SECPLUS1_TARGET_CLOSE, &done) == false);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_CLOSED, SECPLUS1_TARGET_CLOSE, &done) == false);
    assert(done == true);

    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_OPENING, SECPLUS1_TARGET_STOP, &done) == true);
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_STOPPED, SECPLUS1_TARGET_STOP, &done) == false);
    assert(done == true);

    // Toggle target always fires once and finishes.
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_CLOSED, SECPLUS1_TARGET_TOGGLE, &done) == true);
    assert(done == true);

    // None target is trivially done.
    assert(secplus1_pursue_toggle_needed(SECPLUS1_DOOR_UNKNOWN, SECPLUS1_TARGET_NONE, &done) == false);
    assert(done == true);
}

static void test_panel_emu_sequence(void)
{
    tests_run++;
    // Startup sequence from the ratgdo wall panel capture.
    assert(secplus1_panel_emu_byte(0) == 0x35);
    assert(secplus1_panel_emu_byte(1) == 0x35);
    assert(secplus1_panel_emu_byte(5) == 0x33);
    assert(secplus1_panel_emu_byte(8) == 0x38);
    assert(secplus1_panel_emu_byte(12) == 0x39);
    assert(secplus1_panel_emu_byte(14) == 0x3A);
    // Steady state loop repeats the last 4 bytes forever.
    assert(secplus1_panel_emu_byte(15) == 0x38);
    assert(secplus1_panel_emu_byte(16) == 0x3A);
    assert(secplus1_panel_emu_byte(17) == 0x39);
    assert(secplus1_panel_emu_byte(18) == 0x3A);
    assert(secplus1_panel_emu_byte(19) == 0x38);
    assert(secplus1_panel_emu_byte(1000) == secplus1_panel_emu_byte(1000 + 4));
}

static void test_status_poll_sequence(void)
{
    tests_run++;
    // WAITING-phase poll only sends status queries (never toggle presses),
    // so replies are tagged self-replies and can't trip a false panel detect.
    assert(secplus1_status_poll_byte(0) == 0x38);
    assert(secplus1_status_poll_byte(1) == 0x3A);
    assert(secplus1_status_poll_byte(2) == 0x39);
    assert(secplus1_status_poll_byte(3) == 0x3A);
    assert(secplus1_status_poll_byte(4) == 0x38); // wraps
    assert(secplus1_status_poll_byte(100) == secplus1_status_poll_byte(100 + 4));
    // Every byte is a status query, never a toggle/release press (0x30-0x35).
    for (uint32_t i = 0; i < 32; i++)
    {
        uint8_t b = secplus1_status_poll_byte(i);
        assert(b == 0x38 || b == 0x39 || b == 0x3A);
    }
}

int main(void)
{
    test_decode_door_state();
    test_decode_other_status();
    test_rx_parser_single_bytes();
    test_rx_parser_two_byte_packets();
    test_rx_parser_invalid_byte_resyncs();
    test_rx_parser_incomplete_packet_times_out();
    test_rx_parser_real_gdo_reply_bytes();
    test_rx_parser_prime_pairs_own_query_with_reply();
    test_rx_parser_expire_drops_stale_half_packets();
    test_rx_is_self_echo();
    test_parser_echo_pairs_with_prime_and_poisons_door();
    test_rx_parser_valid_range_guard();
    test_pursue_open();
    test_pursue_close_and_stop();
    test_panel_emu_sequence();
    test_status_poll_sequence();
    printf("secplus1: all %d tests passed\n", tests_run);
    return 0;
}
