// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Security+ 2.0 wireline codec - pure logic, host testable. See secplus2.h.
//
// Wire format (all values derived from US20110317835A1 + captures):
//   byte 0..2  header 55 01 00
//   bytes 3..10, 11..18  two halves; each half:
//     byte 0   recovery identifier: 4 ternary pairs sent in the clear.
//                High nibble selects the bit-order pattern, low nibble the
//                inversion pattern (both nibbles must avoid pair value 3).
//     bytes 1..7  2 zero pad bits, then 18 bit-triplets, MSB first. Each
//                triplet carries one bit from each of three 18-bit lanes:
//                lane A = fixed10 + data8, lane B = fixed10 + data8,
//                lane C = roll10 + recovery8. Triplets are permuted by the
//                order pattern and XORed with the inversion flags.
// Half 0 carries the high 20 fixed bits + high 16 data bits, half 1 the low
// parts. Rolling code: 28 bits, bit-mirrored, base-3 expanded to 18 trits;
// half 0 covers trits {3..0, 11..8, 16}, half 1 covers {7..4, 15..12, 17}.

#include "secplus2.h"

// Nibble -> lane permutation / inversion flags. Only nibbles whose two pairs
// avoid value 3 are legal (9 of 16); anything else is rejected on decode.
// Values match zellyn/openers (MIT); table SHAPE per US20110317835A1 Fig. 10.
static const int8_t ORDER_OF[16][3] = {
    {0, 2, 1},     // 0x0
    {2, 0, 1},     // 0x1
    {0, 1, 2},     // 0x2
    {-1, -1, -1},  // 0x3 illegal
    {1, 2, 0},     // 0x4
    {1, 0, 2},     // 0x5
    {2, 1, 0},     // 0x6
    {-1, -1, -1},  // 0x7 illegal
    {1, 2, 0},     // 0x8
    {2, 1, 0},     // 0x9
    {0, 1, 2},     // 0xA
    {-1, -1, -1},  // 0xB illegal
    {-1, -1, -1},  // 0xC illegal
    {-1, -1, -1},  // 0xD illegal
    {-1, -1, -1},  // 0xE illegal
    {-1, -1, -1},  // 0xF illegal
};

static const int8_t INVERT_OF[16][3] = {
    {1, 1, 0},     // 0x0
    {0, 1, 0},     // 0x1
    {0, 0, 1},     // 0x2
    {-1, -1, -1},  // 0x3 illegal
    {1, 1, 1},     // 0x4
    {1, 0, 1},     // 0x5
    {0, 1, 1},     // 0x6
    {-1, -1, -1},  // 0x7 illegal
    {1, 0, 0},     // 0x8
    {0, 0, 0},     // 0x9
    {1, 0, 1},     // 0xA
    {-1, -1, -1},  // 0xB illegal
    {-1, -1, -1},  // 0xC illegal
    {-1, -1, -1},  // 0xD illegal
    {-1, -1, -1},  // 0xE illegal
    {-1, -1, -1},  // 0xF illegal
};

// Trit indices (LSB-first base-3 digits of the mirrored rolling code) carried
// by each half: 4 recovery pairs (sent clear, descending) + 5 scrambled.
static const uint8_t RECOVERY_TRITS[2][4] = {{3, 2, 1, 0}, {7, 6, 5, 4}};
static const uint8_t SCRAMBLED_TRITS[2][5] = {{11, 10, 9, 8, 16},
                                              {15, 14, 13, 12, 17}};

static uint32_t mirror28(uint32_t v)
{
    uint32_t m = 0;
    for (int i = 0; i < 28; i++)
    {
        m |= ((v >> i) & 1u) << (27 - i);
    }
    return m;
}

// Base-3 digits of v, least significant first. Caller needs exactly 18.
static void to_trits(uint32_t v, uint8_t trits[18])
{
    for (int i = 0; i < 18; i++)
    {
        trits[i] = (uint8_t)(v % 3u);
        v /= 3u;
    }
}

static uint32_t from_trits(const uint8_t trits[18])
{
    uint32_t v = 0;
    for (int i = 17; i >= 0; i--)
    {
        v = v * 3u + trits[i];
    }
    return v;
}

// Parity nibble lives at data bits 12..15: fixed-high-nibble XOR every data
// nibble must fold to zero.
static uint32_t with_parity(uint64_t fixed, uint32_t data)
{
    uint32_t nibbles = (uint32_t)((fixed >> 32) & 0xFu);
    data &= 0xFFFF0FFFu;
    for (int shift = 0; shift < 32; shift += 4)
    {
        nibbles ^= (data >> shift) & 0xFu;
    }
    return data | (nibbles << 12);
}

static bool parity_ok(uint64_t fixed, uint32_t data)
{
    uint32_t nibbles = (uint32_t)((fixed >> 32) & 0xFu);
    for (int shift = 0; shift < 32; shift += 4)
    {
        nibbles ^= (data >> shift) & 0xFu;
    }
    return nibbles == 0;
}

// Five trits -> 10 bits, most significant trit first.
static uint32_t pack_pairs(const uint8_t *trits, int n)
{
    uint32_t bits = 0;
    for (int i = 0; i < n; i++)
    {
        bits = (bits << 2) | (trits[i] & 3u);
    }
    return bits;
}

// Ten bits -> five trits, most significant pair first. False on value 3.
static bool unpack_pairs(uint32_t bits, uint8_t *trits, int n)
{
    for (int i = n - 1; i >= 0; i--)
    {
        uint8_t pair = (uint8_t)((bits >> (2 * i)) & 3u);
        if (pair == 3u)
        {
            return false;
        }
        trits[n - 1 - i] = pair;
    }
    return true;
}

int secplus2_encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data,
                             uint8_t packet[SECPLUS2_WIRELINE_LEN])
{
    if (rolling > SECPLUS2_ROLLING_MAX || fixed > SECPLUS2_FIXED_MAX)
    {
        return -1;
    }
    data = with_parity(fixed, data);

    uint8_t trits[18];
    to_trits(mirror28(rolling), trits);

    packet[0] = 0x55;
    packet[1] = 0x01;
    packet[2] = 0x00;

    for (int half = 0; half < 2; half++)
    {
        // Recovery identifier: 4 clear pairs double as pattern selectors.
        uint8_t indicator = 0;
        for (int i = 0; i < 4; i++)
        {
            indicator = (uint8_t)((indicator << 2) |
                                  trits[RECOVERY_TRITS[half][i]]);
        }
        const int8_t *order = ORDER_OF[indicator >> 4];
        const int8_t *invert = INVERT_OF[indicator & 0x0F];

        uint32_t fix20 = (half == 0) ? (uint32_t)(fixed >> 20)
                                     : (uint32_t)(fixed & 0xFFFFFu);
        uint32_t dat16 =
            (half == 0) ? (data >> 16) & 0xFFFFu : data & 0xFFFFu;
        uint8_t ftrits[5];
        for (int i = 0; i < 5; i++)
        {
            ftrits[i] = trits[SCRAMBLED_TRITS[half][i]];
        }
        uint32_t lanes[3];
        lanes[0] = ((fix20 >> 10) << 8) | (dat16 >> 8);
        lanes[1] = (((fix20 >> 0) & 0x3FFu) << 8) | (dat16 & 0xFFu);
        lanes[2] = (pack_pairs(ftrits, 5) << 8) | indicator;

        uint8_t *dst = &packet[3 + half * 8];
        dst[0] = indicator;
        // Two zero pad bits, then 18 triplets MSB first.
        uint32_t bitpos = 0; // counts payload bits after the pad
        for (int i = 0; i < 7; i++)
        {
            dst[1 + i] = 0;
        }
        for (int t = 0; t < 18; t++)
        {
            for (int j = 0; j < 3; j++)
            {
                int lane = order[j];
                uint8_t bit =
                    (uint8_t)(((lanes[lane] >> (17 - t)) & 1u) ^ invert[j]);
                uint32_t at = 2 + bitpos; // absolute bit offset in bytes 1..7
                dst[1 + at / 8] |= (uint8_t)(bit << (7 - (at % 8)));
                bitpos++;
            }
        }
    }
    return 0;
}

int secplus2_decode_wireline(const uint8_t packet[SECPLUS2_WIRELINE_LEN],
                             uint32_t *rolling, uint64_t *fixed,
                             uint32_t *data)
{
    if (packet[0] != 0x55 || packet[1] != 0x01 || packet[2] != 0x00)
    {
        return -1;
    }

    uint8_t trits[18];
    uint32_t fix20[2];
    uint32_t dat16[2];

    for (int half = 0; half < 2; half++)
    {
        const uint8_t *src = &packet[3 + half * 8];
        uint8_t indicator = src[0];
        if (ORDER_OF[indicator >> 4][0] < 0 ||
            INVERT_OF[indicator & 0x0F][0] < 0)
        {
            return -1;
        }
        const int8_t *order = ORDER_OF[indicator >> 4];
        const int8_t *invert = INVERT_OF[indicator & 0x0F];

        // Un-permute triplets back into lanes (pad bits are bytes1 top two).
        uint32_t lanes[3] = {0, 0, 0};
        // Inverse permutation: lane k arrives at triplet position j.
        int8_t at[3];
        for (int j = 0; j < 3; j++)
        {
            at[order[j]] = (int8_t)j;
        }
        for (int t = 0; t < 18; t++)
        {
            for (int k = 0; k < 3; k++)
            {
                uint32_t pos = 2 + 3u * (uint32_t)t + (uint32_t)at[k];
                uint8_t bit =
                    (uint8_t)((src[1 + pos / 8] >> (7 - (pos % 8))) & 1u);
                lanes[k] = (lanes[k] << 1) | (bit ^ (uint8_t)invert[at[k]]);
            }
        }

        uint8_t erec = (uint8_t)(lanes[2] & 0xFFu);
        if (erec != indicator)
        {
            return -1;
        }
        uint8_t epairs[4];
        if (!unpack_pairs(indicator, epairs, 4))
        {
            return -1;
        }
        uint8_t fpairs[5];
        if (!unpack_pairs((lanes[2] >> 8) & 0x3FFu, fpairs, 5))
        {
            return -1;
        }
        for (int i = 0; i < 4; i++)
        {
            trits[RECOVERY_TRITS[half][i]] = epairs[i];
        }
        for (int i = 0; i < 5; i++)
        {
            trits[SCRAMBLED_TRITS[half][i]] = fpairs[i];
        }

        fix20[half] = (((lanes[0] >> 8) & 0x3FFu) << 10) |
                      ((lanes[1] >> 8) & 0x3FFu);
        dat16[half] =
            (((lanes[0] >> 0) & 0xFFu) << 8) | ((lanes[1] >> 0) & 0xFFu);
    }

    uint32_t r = mirror28(from_trits(trits));
    if (r > SECPLUS2_ROLLING_MAX)
    {
        return -1;
    }
    uint64_t f = ((uint64_t)fix20[0] << 20) | fix20[1];
    uint32_t d = (dat16[0] << 16) | dat16[1];
    if (!parity_ok(f, d))
    {
        return -1;
    }
    if (rolling)
    {
        *rolling = r;
    }
    if (fixed)
    {
        *fixed = f;
    }
    if (data)
    {
        *data = d;
    }
    return 0;
}

int secplus2_encode_command(uint32_t rolling, uint64_t device_id,
                            uint16_t command, uint32_t payload,
                            uint8_t packet[SECPLUS2_WIRELINE_LEN])
{
    if ((device_id >> 40) != 0 || (command >> 12) != 0 ||
        (payload >> 20) != 0)
    {
        return -1;
    }
    uint64_t fixed =
        (device_id & 0xF0FFFFFFFFULL) | ((uint64_t)(command & 0xF00) << 24);
    uint32_t data = ((payload & 0xFFu) << 24) | ((payload & 0xFF00u) << 8) |
                    ((payload & 0xF0000u) >> 8) | (command & 0xFFu);
    return secplus2_encode_wireline(rolling, fixed, data, packet);
}

int secplus2_decode_command(const uint8_t packet[SECPLUS2_WIRELINE_LEN],
                            uint32_t *rolling, uint64_t *device_id,
                            uint16_t *command, uint32_t *payload)
{
    uint32_t r;
    uint64_t f;
    uint32_t d;
    if (secplus2_decode_wireline(packet, &r, &f, &d) != 0)
    {
        return -1;
    }
    if (rolling)
    {
        *rolling = r;
    }
    if (device_id)
    {
        *device_id = f & 0xF0FFFFFFFFULL;
    }
    if (command)
    {
        *command = (uint16_t)(((f >> 24) & 0xF00ULL) | (d & 0xFFu));
    }
    if (payload)
    {
        *payload = ((d << 8) & 0xF0000u) | ((d >> 8) & 0xFF00u) | (d >> 24);
    }
    return 0;
}
