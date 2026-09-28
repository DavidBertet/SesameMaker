// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the Improv Wi-Fi serial codec: framing,
// checksum, resync, and RPC payload codecs.

#include "../src/core/improv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;

// Feed a whole buffer; returns true if a frame completed.
static bool feed_all(improv_parser_t *p, const uint8_t *buf, size_t len)
{
    bool done = false;
    for (size_t i = 0; i < len; i++)
    {
        if (improv_parser_feed(p, buf[i]))
        {
            done = true;
        }
    }
    return done;
}

static void test_build_request_state_vector(void)
{
    tests_run++;
    // RPC RequestState: cmd 0x02, empty payload -> data [02 00], len 2.
    // Checksum: magic(477) + 01 + 03 + 02 + 02 + 00 = 485 & 0xFF = 0xE5.
    uint8_t rpc[] = {IMPROV_RPC_GET_STATE, 0x00};
    uint8_t out[32];
    size_t n = improv_frame(IMPROV_TYPE_RPC, rpc, sizeof(rpc), out, sizeof(out));
    assert(n == 13);
    const uint8_t want[] = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x03, 0x02,
                            0x02, 0x00, 0xE5, '\n'};
    assert(n == sizeof(want));
    assert(memcmp(out, want, n) == 0);
}

static void test_frame_overflow(void)
{
    tests_run++;
    uint8_t out[16];
    uint8_t data[4] = {1, 2, 3, 4};
    assert(improv_frame(IMPROV_TYPE_STATE, data, sizeof(data), out, 10) == 0);
    assert(improv_frame(IMPROV_TYPE_STATE, data, 256, out, sizeof(out)) == 0);
}

static void test_parse_round_trip(void)
{
    tests_run++;
    uint8_t payload[] = {IMPROV_RPC_WIFI_SETTINGS, 0x03, 0x03, 'a', 'b', 'c', 0x02, 'x', 'y'};
    uint8_t frame[64];
    size_t n = improv_frame(IMPROV_TYPE_RPC, payload, sizeof(payload), frame, sizeof(frame));
    assert(n > 0);
    improv_parser_t p;
    improv_parser_init(&p);
    assert(feed_all(&p, frame, n));
    assert(improv_frame_type(&p) == IMPROV_TYPE_RPC);
    assert(improv_frame_len(&p) == sizeof(payload));
    assert(memcmp(improv_frame_data(&p), payload, sizeof(payload)) == 0);
}

static void test_parse_resyncs_through_noise(void)
{
    tests_run++;
    uint8_t rpc[] = {IMPROV_RPC_GET_STATE, 0x00};
    uint8_t frame[32];
    size_t n = improv_frame(IMPROV_TYPE_RPC, rpc, sizeof(rpc), frame, sizeof(frame));
    uint8_t noisy[64];
    // Log-line noise, a partial magic, then the real frame.
    size_t pos = 0;
    memcpy(noisy + pos, "boot: ok\nIMPR", 12);
    pos += 12;
    memcpy(noisy + pos, frame, n);
    pos += n;
    improv_parser_t p;
    improv_parser_init(&p);
    assert(feed_all(&p, noisy, pos));
    assert(improv_frame_type(&p) == IMPROV_TYPE_RPC);
}

static void test_parse_rejects_bad_checksum_and_version(void)
{
    tests_run++;
    uint8_t rpc[] = {IMPROV_RPC_GET_STATE, 0x00};
    uint8_t frame[32];
    size_t n = improv_frame(IMPROV_TYPE_RPC, rpc, sizeof(rpc), frame, sizeof(frame));
    improv_parser_t p;
    improv_parser_init(&p);
    frame[n - 2] ^= 0xFF; // corrupt checksum (byte before '\n')
    assert(!feed_all(&p, frame, n));
    // A good frame right after still parses (resync works).
    n = improv_frame(IMPROV_TYPE_RPC, rpc, sizeof(rpc), frame, sizeof(frame));
    assert(feed_all(&p, frame, n));

    improv_parser_init(&p);
    frame[6] = 0x02; // bad version; fix checksum over the first 11 bytes
    uint8_t cksum = 0;
    for (size_t i = 0; i < n - 2; i++)
    {
        cksum += frame[i];
    }
    frame[n - 2] = cksum;
    assert(!feed_all(&p, frame, n));
}

static void test_rpc_parse(void)
{
    tests_run++;
    uint8_t cmd;
    const uint8_t *payload;
    size_t plen;
    uint8_t ok[] = {0x01, 0x02, 0xAA, 0xBB};
    assert(improv_rpc_parse(ok, sizeof(ok), &cmd, &payload, &plen));
    assert(cmd == 0x01 && plen == 2 && payload[0] == 0xAA);
    uint8_t short_buf[] = {0x01};
    assert(!improv_rpc_parse(short_buf, sizeof(short_buf), &cmd, &payload, &plen));
    uint8_t over[] = {0x01, 0x05, 0xAA};
    assert(!improv_rpc_parse(over, sizeof(over), &cmd, &payload, &plen));
}

static void test_wifi_settings_parse(void)
{
    tests_run++;
    char ssid[33], pass[65];
    // ssid "ab", password "xyz".
    uint8_t good[] = {0x02, 'a', 'b', 0x03, 'x', 'y', 'z'};
    assert(improv_wifi_settings_parse(good, sizeof(good), ssid, 32, pass, 64));
    assert(strcmp(ssid, "ab") == 0 && strcmp(pass, "xyz") == 0);
    // Empty password is legal (open network).
    uint8_t open[] = {0x02, 'a', 'b', 0x00};
    assert(improv_wifi_settings_parse(open, sizeof(open), ssid, 32, pass, 64));
    assert(strcmp(pass, "") == 0);
    // Empty ssid rejected.
    uint8_t no_ssid[] = {0x00, 0x01, 'x'};
    assert(!improv_wifi_settings_parse(no_ssid, sizeof(no_ssid), ssid, 32, pass, 64));
    // Truncated / overrunning lengths rejected.
    uint8_t trunc[] = {0x05, 'a', 'b'};
    assert(!improv_wifi_settings_parse(trunc, sizeof(trunc), ssid, 32, pass, 64));
    // Over-small buffers rejected (no partial copy).
    assert(!improv_wifi_settings_parse(good, sizeof(good), ssid, 1, pass, 64));
}

static void test_ota_password_parse(void)
{
    tests_run++;
    char pass[65];
    uint8_t set[] = {0x03, 'p', 'w', 'd'};
    assert(improv_ota_password_parse(set, sizeof(set), pass, 64));
    assert(strcmp(pass, "pwd") == 0);
    // Length 0 clears the password.
    uint8_t clear[] = {0x00};
    assert(improv_ota_password_parse(clear, sizeof(clear), pass, 64));
    assert(strcmp(pass, "") == 0);
    uint8_t too_long[] = {65};
    assert(!improv_ota_password_parse(too_long, sizeof(too_long), pass, 3));
}

static void test_result_payload(void)
{    tests_run++;
    uint8_t out[64];
    const char *strs[] = {"SesameMaker", "dev", "esp32-c6", "SesameMaker"};
    size_t n = improv_result_payload(IMPROV_RPC_GET_INFO, strs, 4, out, sizeof(out));
    assert(n > 0 && out[0] == IMPROV_RPC_GET_INFO);
    // Walk the string list back.
    size_t pos = 2;
    for (size_t i = 0; i < 4; i++)
    {
        size_t slen = out[pos++];
        assert(pos + slen <= n);
        assert(strlen(strs[i]) == slen && memcmp(out + pos, strs[i], slen) == 0);
        pos += slen;
    }
    assert(pos == n && out[1] == (uint8_t)(n - 2));
    // Empty list and overflow.
    assert(improv_result_payload(IMPROV_RPC_GET_INFO, NULL, 0, out, sizeof(out)) == 2);
    uint8_t tiny[4];
    assert(improv_result_payload(IMPROV_RPC_GET_INFO, strs, 4, tiny, sizeof(tiny)) == 0);
}

static void test_network_reset_rpc_round_trip(void)
{
    tests_run++;
    // Empty payload request: data [0x12 00], parses back to the same cmd.
    uint8_t rpc[] = {IMPROV_RPC_NETWORK_RESET, 0x00};
    uint8_t frame[32];
    size_t n = improv_frame(IMPROV_TYPE_RPC, rpc, sizeof(rpc), frame, sizeof(frame));
    assert(n > 0);
    improv_parser_t p;
    improv_parser_init(&p);
    assert(feed_all(&p, frame, n));
    assert(improv_frame_type(&p) == IMPROV_TYPE_RPC);
    uint8_t cmd;
    const uint8_t *payload;
    size_t plen;
    assert(improv_rpc_parse(improv_frame_data(&p), improv_frame_len(&p), &cmd, &payload, &plen));
    assert(cmd == IMPROV_RPC_NETWORK_RESET && plen == 0);
    // Empty result ack encodes to the 2-byte [cmd 00] payload.
    uint8_t out[64];
    assert(improv_result_payload(IMPROV_RPC_NETWORK_RESET, NULL, 0, out, sizeof(out)) == 2);
    assert(out[0] == IMPROV_RPC_NETWORK_RESET && out[1] == 0x00);
}

int main(void)
{
    test_build_request_state_vector();
    test_frame_overflow();
    test_parse_round_trip();
    test_parse_resyncs_through_noise();
    test_parse_rejects_bad_checksum_and_version();
    test_rpc_parse();
    test_wifi_settings_parse();
    test_ota_password_parse();
    test_result_payload();
    test_network_reset_rpc_round_trip();
    printf("test_improv: %d tests passed\n", tests_run);
    return 0;
}
