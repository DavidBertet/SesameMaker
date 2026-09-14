// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Host-side unit tests for the raw bus-traffic JSON builder.

#include "../src/app/raw_json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                         \
        {                                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_payload_content(void)
{
    raw_json_event_t rx[4] = {{1000, 0x28}, {2000, 0x4A}, {0, 0}, {4000, 0x11}};
    raw_json_event_t tx[2] = {{3000, 0x4A}, {0, 0}};

    size_t need = raw_json_garage_payload(NULL, 0, rx, 4, 0, 1,
                                          tx, 2, 0, 2);
    char *buf = malloc(need + 1);
    CHECK(buf != NULL);
    raw_json_garage_payload(buf, need + 1, rx, 4, 0, 1, tx, 2, 0, 2);

    CHECK(strlen(buf) == need);
    CHECK(strstr(buf, "\"type\":\"garage_raw\"") != NULL);
    CHECK(strstr(buf, "\"rx\":[{\"t\":1000,\"byte\":40},"
                      "{\"t\":2000,\"byte\":74},{\"t\":4000,\"byte\":17}]") != NULL);
    CHECK(strstr(buf, "\"tx\":[{\"t\":3000,\"byte\":74}]") != NULL);
    CHECK(strstr(buf, "\"rx_total\":1,\"tx_total\":2}") != NULL);
    free(buf);
    return 0;
}

static int test_oldest_first_across_wrap(void)
{
    // head=1: write position (oldest valid) is index 1, wrapping through 0.
    raw_json_event_t rx[3] = {{0, 0}, {700, 0xAA}, {800, 0xBB}};

    size_t need = raw_json_garage_payload(NULL, 0, rx, 3, 1, 0,
                                          NULL, 0, 0, 0);
    char *buf = malloc(need + 1);
    CHECK(buf != NULL);
    raw_json_garage_payload(buf, need + 1, rx, 3, 1, 0, NULL, 0, 0, 0);

    CHECK(strstr(buf, "\"rx\":[{\"t\":700,\"byte\":170},"
                      "{\"t\":800,\"byte\":187}]") != NULL);
    free(buf);
    return 0;
}

static int test_truncation_is_safe(void)
{
    raw_json_event_t rx[2] = {{9, 1}, {8, 2}};
    char buf[9]; /* canary at index 8, one byte past the size we pass */
    memset(buf, 0xEE, sizeof(buf));

    size_t off = raw_json_garage_payload(buf, 8, rx, 2, 0, 0,
                                         NULL, 0, 0, 0);
    CHECK(off > 0);
    CHECK((unsigned char)buf[8] == 0xEE); /* no overflow */
    CHECK(memchr(buf, '\0', 8) != NULL);  /* terminated in range */
    return 0;
}

int main(void)
{
    return test_payload_content() |
           test_oldest_first_across_wrap() |
           test_truncation_is_safe();
}