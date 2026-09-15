// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the native secplus2 wireline codec (MIT).
// Vectors are over-the-wire captures (rolling/fixed/data + 19-byte packets).

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/app/secplus2.h"

static int tests_run = 0;

#define NVEC 16

static const uint32_t V_ROLLING[NVEC] = {
    1375, 1376, 1377, 1378, 1379, 1380, 1381, 1382,
    732, 733, 734, 735, 736, 737, 738, 739,
};

static const uint64_t V_FIXED[NVEC] = {
    0xc21895590cULL, 0xc11895590cULL, 0xc11895590cULL, 0xc11895590cULL,
    0xc11895590cULL, 0xc31895590cULL, 0xc11895590cULL, 0xc31895590cULL,
    0x10aad8002eULL, 0x10aad8002eULL, 0x10aad8002eULL, 0x10aad8002eULL,
    0x13aad8002eULL, 0x13aad8002eULL, 0x13aad8002eULL, 0x13aad8002eULL,
};

static const uint32_t V_DATA[NVEC] = {
    0xf085, 0x728c, 0x728c, 0x728c,
    0x728c, 0xa191, 0x8081, 0x8092,
    0x360e281, 0x260f281, 0x360e281, 0x260f281,
    0x8193, 0x8193, 0x8193, 0x8193,
};

static const uint8_t V_CODE[NVEC][19] = {
    {0x55, 0x01, 0x00, 0x5a, 0x3a, 0x32, 0xc7, 0x29, 0xb2, 0xc9, 0x65, 0x8a, 0x28, 0xb3, 0xc6, 0x35, 0x52, 0x4e, 0x79},
    {0x55, 0x01, 0x00, 0x15, 0x3e, 0x96, 0x91, 0xec, 0xb6, 0x9a, 0x69, 0x92, 0x0a, 0x30, 0xc7, 0x81, 0xe0, 0x46, 0xe1},
    {0x55, 0x01, 0x00, 0x14, 0x28, 0x5f, 0xb5, 0x7e, 0xff, 0xbe, 0xff, 0x84, 0x3a, 0xbb, 0xc6, 0x35, 0x3b, 0x66, 0x77},
    {0x55, 0x01, 0x00, 0x6a, 0x22, 0xf6, 0xde, 0x2b, 0xa4, 0xd3, 0x4d, 0x22, 0x07, 0x34, 0xc7, 0xa9, 0x76, 0x56, 0xe1},
    {0x55, 0x01, 0x00, 0x69, 0x2b, 0xc0, 0x05, 0x46, 0x12, 0x08, 0x04, 0x14, 0x1c, 0xe7, 0x9c, 0x2b, 0xd6, 0x9c, 0xaf},
    {0x55, 0x01, 0x00, 0x48, 0x29, 0x1b, 0x47, 0x60, 0x9a, 0x4d, 0x24, 0x02, 0x1c, 0x74, 0xf1, 0xa9, 0x86, 0x82, 0x5c},
    {0x55, 0x01, 0x00, 0x46, 0x06, 0xec, 0xbe, 0x1d, 0x65, 0xb6, 0x4b, 0xa4, 0x39, 0xef, 0xaa, 0x1e, 0x1f, 0x7f, 0xbd},
    {0x55, 0x01, 0x00, 0xa1, 0x35, 0x0d, 0x39, 0x98, 0x69, 0xa4, 0x93, 0x52, 0x0e, 0x28, 0xab, 0x99, 0xe0, 0xc2, 0x61},
    {0x55, 0x01, 0x00, 0xa4, 0x27, 0x8e, 0xfb, 0x1c, 0xd6, 0x7f, 0x9b, 0x28, 0x05, 0x93, 0xcf, 0xb4, 0x40, 0xcb, 0x06},
    {0x55, 0x01, 0x00, 0xa2, 0x18, 0x71, 0x96, 0xaa, 0x0d, 0x12, 0x61, 0x19, 0x14, 0x80, 0x82, 0x4c, 0x69, 0x68, 0x15},
    {0x55, 0x01, 0x00, 0x49, 0x20, 0x8e, 0x2d, 0x0c, 0x1a, 0x04, 0x0b, 0x89, 0x08, 0x02, 0x09, 0x22, 0xe4, 0x84, 0x0e},
    {0x55, 0x01, 0x00, 0x48, 0x14, 0x14, 0x64, 0x28, 0x88, 0x4d, 0x2c, 0x6a, 0x25, 0xa7, 0xdf, 0xdd, 0xc0, 0x43, 0x47},
    {0x55, 0x01, 0x00, 0x02, 0x01, 0x55, 0x19, 0x28, 0x24, 0x92, 0x59, 0x82, 0x01, 0x2e, 0xbf, 0x69, 0xc4, 0xd2, 0x7c},
    {0x55, 0x01, 0x00, 0x01, 0x0a, 0x70, 0xaf, 0xf3, 0x49, 0x24, 0x90, 0x64, 0x37, 0x7e, 0xb3, 0x6b, 0x8d, 0xde, 0xec},
    {0x55, 0x01, 0x00, 0x58, 0x05, 0x38, 0x46, 0xb1, 0x96, 0x5b, 0x24, 0x12, 0x39, 0x24, 0x58, 0x01, 0x44, 0xc2, 0x62},
    {0x55, 0x01, 0x00, 0x56, 0x3b, 0xe3, 0xb9, 0x4e, 0x69, 0xa6, 0x93, 0x04, 0x0f, 0xf6, 0x7d, 0xb5, 0x5f, 0xef, 0x72},
};

static const uint16_t V_COMMAND[NVEC] = {
    0x285, 0x18c, 0x18c, 0x18c, 0x18c, 0x391, 0x181, 0x392,
    0x081, 0x081, 0x081, 0x081, 0x393, 0x393, 0x393, 0x393,
};

static const uint32_t V_PAYLOAD[NVEC] = {
    0x00000, 0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x00000, 0x00000,
    0x26003, 0x26002, 0x26003, 0x26002, 0x10000, 0x10000, 0x10000, 0x10000,
};

static void test_encode_vectors(void)
{
    tests_run++;
    for (int i = 0; i < NVEC; i++)
    {
        uint8_t packet[SECPLUS2_WIRELINE_LEN];
        assert(secplus2_encode_wireline(V_ROLLING[i], V_FIXED[i], V_DATA[i],
                                        packet) == 0);
        assert(memcmp(packet, V_CODE[i], SECPLUS2_WIRELINE_LEN) == 0);
    }
}

static void test_decode_vectors(void)
{
    tests_run++;
    for (int i = 0; i < NVEC; i++)
    {
        uint32_t rolling = 0;
        uint64_t fixed = 0;
        uint32_t data = 0;
        assert(secplus2_decode_wireline(V_CODE[i], &rolling, &fixed, &data) ==
               0);
        assert(rolling == V_ROLLING[i]);
        assert(fixed == V_FIXED[i]);
        assert(data == V_DATA[i]);
    }
}

static void test_command_vectors(void)
{
    tests_run++;
    for (int i = 0; i < NVEC; i++)
    {
        uint64_t device_id = V_FIXED[i] & 0xF0FFFFFFFFULL;
        uint8_t packet[SECPLUS2_WIRELINE_LEN];
        assert(secplus2_encode_command(V_ROLLING[i], device_id, V_COMMAND[i],
                                       V_PAYLOAD[i], packet) == 0);
        assert(memcmp(packet, V_CODE[i], SECPLUS2_WIRELINE_LEN) == 0);

        uint32_t rolling = 0;
        uint64_t device_out = 0;
        uint16_t command = 0;
        uint32_t payload = 0;
        assert(secplus2_decode_command(V_CODE[i], &rolling, &device_out,
                                       &command, &payload) == 0);
        assert(rolling == V_ROLLING[i]);
        assert(device_out == device_id);
        assert(command == V_COMMAND[i]);
        assert(payload == V_PAYLOAD[i]);
    }
}

static void test_limits(void)
{
    tests_run++;
    uint8_t packet[SECPLUS2_WIRELINE_LEN];
    assert(secplus2_encode_wireline(1u << 28, 0, 0, packet) < 0);
    assert(secplus2_encode_wireline(0, 1ULL << 40, 0, packet) < 0);
    assert(secplus2_encode_wireline((1u << 28) - 1, (1ULL << 40) - 1, 0,
                                    packet) == 0);
    assert(secplus2_encode_command(1u << 28, 0, 0, 0, packet) < 0);
    assert(secplus2_encode_command(0, 0, 1u << 12, 0, packet) < 0);
    assert(secplus2_encode_command(0, 0, 0, 1u << 20, packet) < 0);
}

static void test_bad_packets(void)
{
    tests_run++;
    uint8_t bad[SECPLUS2_WIRELINE_LEN];
    uint32_t rolling = 0;
    uint64_t fixed = 0;
    uint32_t data = 0;
    memcpy(bad, V_CODE[0], sizeof(bad));
    bad[0] = 0x00; // header must be 55 01 00
    assert(secplus2_decode_wireline(bad, &rolling, &fixed, &data) < 0);
    memcpy(bad, V_CODE[0], sizeof(bad));
    bad[5] ^= 0x01; // corrupted payload breaks parity/unscramble
    assert(secplus2_decode_wireline(bad, &rolling, &fixed, &data) < 0);
}

// Deterministic PRNG (xorshift) so fuzz is reproducible without libc rand.
static uint32_t rng_state = 0x2545F491u;
static uint32_t next_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void test_roundtrip_fuzz(void)
{
    tests_run++;
    for (int i = 0; i < 2000; i++)
    {
        uint32_t rolling = next_rand() & SECPLUS2_ROLLING_MAX;
        uint64_t fixed =
            ((uint64_t)next_rand() << 32 | next_rand()) & SECPLUS2_FIXED_MAX;
        uint32_t data = next_rand();
        uint8_t packet[SECPLUS2_WIRELINE_LEN];
        assert(secplus2_encode_wireline(rolling, fixed, data, packet) == 0);
        uint32_t r2 = 0;
        uint64_t f2 = 0;
        uint32_t d2 = 0;
        assert(secplus2_decode_wireline(packet, &r2, &f2, &d2) == 0);
        assert(r2 == rolling);
        assert(f2 == fixed);
        // Data bits 12..15 carry parity, recomputed on encode.
        assert((d2 & 0xFFFF0FFFu) == (data & 0xFFFF0FFFu));
    }
}

static void test_framer(void);
static void test_status_parse(void);
static void test_builders(void);

int main(void)
{
    test_encode_vectors();
    test_decode_vectors();
    test_command_vectors();
    test_limits();
    test_bad_packets();
    test_roundtrip_fuzz();
    test_framer();
    test_status_parse();
    test_builders();
    printf("test_secplus2: %d test groups passed\n", tests_run);
    return 0;
}

static void test_framer(void)
{
    tests_run++;
    secplus2_framer_t f;
    uint8_t out[SECPLUS2_WIRELINE_LEN];
    secplus2_framer_init(&f);

    // Garbage before a frame is ignored (hunt only advances on 55/01/00).
    const uint8_t garbage[] = {0x00, 0xFF, 0x55, 0x12, 0x55, 0x01, 0x44};
    for (unsigned i = 0; i < sizeof(garbage); i++)
    {
        assert(secplus2_framer_feed(&f, garbage[i], 1000 + i, out) == false);
    }
    // Feed a full capture in two chunks: no packet until the last byte.
    for (int i = 0; i < 19; i++)
    {
        bool done = secplus2_framer_feed(&f, V_CODE[3][i], 2000 + i, out);
        assert(done == (i == 18));
    }
    assert(memcmp(out, V_CODE[3], SECPLUS2_WIRELINE_LEN) == 0);

    // Re-sync mid-packet restarts the frame instead of emitting garbage.
    secplus2_framer_init(&f);
    assert(secplus2_framer_feed(&f, 0x55, 3000, out) == false);
    assert(secplus2_framer_feed(&f, 0x01, 3001, out) == false);
    assert(secplus2_framer_feed(&f, 0x00, 3002, out) == false);
    assert(secplus2_framer_feed(&f, 0xAA, 3003, out) == false);
    assert(secplus2_framer_feed(&f, 0xBB, 3004, out) == false);
    assert(secplus2_framer_feed(&f, 0x55, 3005, out) == false);
    assert(secplus2_framer_feed(&f, 0x01, 3006, out) == false);
    assert(secplus2_framer_feed(&f, 0x00, 3007, out) == false); // restart
    for (int i = 3; i < 19; i++)
    {
        bool done = secplus2_framer_feed(&f, V_CODE[5][i], 3008 + i, out);
        assert(done == (i == 18));
    }
    assert(memcmp(out, V_CODE[5], SECPLUS2_WIRELINE_LEN) == 0);

    // Stale partials expire after the idle timeout.
    secplus2_framer_init(&f);
    assert(secplus2_framer_feed(&f, 0x55, 4000, out) == false);
    assert(secplus2_framer_feed(&f, 0x01, 4001, out) == false);
    secplus2_framer_expire(&f, 4001 + SECPLUS2_RX_IDLE_TIMEOUT_MS + 1);
    // The 0x00 below must NOT complete a frame (hunt restarted).
    assert(secplus2_framer_feed(&f, 0x00, 4200, out) == false);
}

static void test_status_parse(void)
{
    tests_run++;
    // Door nibble map 1..5, everything else unknown.
    assert(secplus2_door_from_nibble(1) == SECPLUS1_DOOR_OPEN);
    assert(secplus2_door_from_nibble(2) == SECPLUS1_DOOR_CLOSED);
    assert(secplus2_door_from_nibble(3) == SECPLUS1_DOOR_STOPPED);
    assert(secplus2_door_from_nibble(4) == SECPLUS1_DOOR_OPENING);
    assert(secplus2_door_from_nibble(5) == SECPLUS1_DOOR_CLOSING);
    assert(secplus2_door_from_nibble(0) == SECPLUS1_DOOR_UNKNOWN);
    assert(secplus2_door_from_nibble(9) == SECPLUS1_DOOR_UNKNOWN);

    // Non-STATUS commands are refused.
    secplus2_status_t st;
    assert(secplus2_parse_status(SECPLUS2_CMD_MOTION, 1, 0, 0, &st) == false);

    // End to end: build a STATUS reply, decode it, parse fields.
    // nibble=2 (closed), byte1 bit6 set (clear), byte2 = light|lock bits.
    uint8_t pkt[SECPLUS2_WIRELINE_LEN];
    uint32_t payload = 0x42u | ((uint32_t)0x40 << 8) | ((uint32_t)0x02 << 16);
    assert(secplus2_encode_command(777, SECPLUS2_CLIENT_ID_DEFAULT,
                                   SECPLUS2_CMD_STATUS, payload,
                                   pkt) == 0);
    // Parse from wire DATA bytes (nibble lives at data bits 8..11, beside
    // the parity nibble), not from the packed command payload.
    uint32_t r = 0;
    uint64_t dev = 0;
    uint32_t d = 0;
    assert(secplus2_decode_wireline(pkt, &r, &dev, &d) == 0);
    uint16_t cmd = (uint16_t)(((dev >> 24) & 0xF00ULL) | (d & 0xFFu));
    assert(cmd == SECPLUS2_CMD_STATUS);
    assert(secplus2_parse_status(cmd, (d >> 8) & 0xFF, (d >> 16) & 0xFF,
                                 (d >> 24) & 0xFF, &st) == true);
    assert(st.door == SECPLUS1_DOOR_CLOSED);
    assert(st.light == 1); // byte2 bit1
    assert(st.lock == 0);  // byte2 bit0
    assert(st.obstruction == false);
    assert(st.learn == false);

    // Obstructed + locked + learn-active variant.
    payload = 0x21u | ((uint32_t)0x00 << 8) | ((uint32_t)0x04 << 16);
    assert(secplus2_encode_command(778, SECPLUS2_CLIENT_ID_DEFAULT,
                                   SECPLUS2_CMD_STATUS, payload,
                                   pkt) == 0);
    assert(secplus2_decode_wireline(pkt, &r, &dev, &d) == 0);
    cmd = (uint16_t)(((dev >> 24) & 0xF00ULL) | (d & 0xFFu));
    assert(cmd == SECPLUS2_CMD_STATUS);
    assert(secplus2_parse_status(cmd, (d >> 8) & 0xFF, (d >> 16) & 0xFF,
                                 (d >> 24) & 0xFF, &st) == true);
    assert(st.door == SECPLUS1_DOOR_OPENING);
    assert(st.light == 0);
    assert(st.lock == 1);
    assert(st.obstruction == true); // byte1 bit6 clear = blocked
    assert(st.learn == true);       // byte2 bit5
}

static void test_builders(void)
{
    tests_run++;
    secplus2_tx_t tx = secplus2_build_door(SECPLUS2_DOOR_OPEN, 0);
    assert(tx.command == SECPLUS2_CMD_DOOR_ACTION);
    assert(tx.payload == (1u | ((uint32_t)1 << 8) | ((uint32_t)1 << 16)));
    tx = secplus2_build_door(SECPLUS2_DOOR_OPEN, 1);
    assert(tx.payload == (1u | ((uint32_t)0 << 8) | ((uint32_t)1 << 16)));
    tx = secplus2_build_light(SECPLUS2_TOGGLE);
    assert(tx.command == SECPLUS2_CMD_LIGHT && tx.payload == 2);
    tx = secplus2_build_lock(SECPLUS2_ON_LOCK);
    assert(tx.command == SECPLUS2_CMD_LOCK && tx.payload == 1);
    tx = secplus2_build_get_status();
    assert(tx.command == SECPLUS2_CMD_GET_STATUS && tx.payload == 0);
}
