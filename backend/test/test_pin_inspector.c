// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Host-side unit tests for the generic pin/bus inspector: payload builder +
// framing analysis over synthetic edge waveforms.

#include "../src/core/inspector/pin_inspector.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                         \
        }                                                                     \
    } while (0)

// {gpio, level, t_us}
#define E(g, lv, t) {(uint8_t)(g), (uint8_t)(lv), (uint32_t)(t)}

static int test_type_strings(void)
{
    CHECK(strcmp(pin_type_str(PIN_TYPE_DIGITAL), "digital") == 0);
    CHECK(strcmp(pin_type_str(PIN_TYPE_ANALOG), "analog") == 0);
    CHECK(strcmp(pin_type_str(PIN_TYPE_UART), "uart") == 0);
    CHECK(strcmp(bus_type_str(BUS_UART), "uart") == 0);
    CHECK(strcmp(bus_type_str(BUS_I2C), "i2c") == 0);
    CHECK(strcmp(bus_type_str(BUS_SPI), "spi") == 0);
    return 0;
}

static int test_hit_logic(void)
{
    CHECK(pin_hit_of(0, true) == true); // active-low reed: low = hit
    CHECK(pin_hit_of(1, true) == false);
    CHECK(pin_hit_of(1, false) == true); // active-high: high = hit
    CHECK(pin_hit_of(0, false) == false);
    return 0;
}

static int test_pin_payload(void)
{
    pin_desc_t descs[3] = {
        {.gpio = 4, .name = "TX", .role = "bus tx", .type = PIN_TYPE_UART,
         .is_output = true, .detail = "1200 baud 8E1"},
        {.gpio = 17, .name = "REED", .role = "reed", .type = PIN_TYPE_DIGITAL,
         .is_output = false, .active_low = true, .report_hit = true},
        {.gpio = 32, .name = "BATT", .role = "battery", .type = PIN_TYPE_ANALOG},
    };
    pin_sample_t samples[3] = {
        {.level = 0},
        {.level = 0, .has_hit = true, .hit = true, .edges = 3,
         .has_activity = true, .idle_ms = 1200},
        {.has_analog = true, .raw = 2345, .mv = 1820},
    };
    size_t need = gpio_state_format(NULL, 0, descs, samples, 3, NULL, NULL, 0, 0);
    char *buf = malloc(need + 1);
    CHECK(buf != NULL);
    gpio_state_format(buf, need + 1, descs, samples, 3, NULL, NULL, 0, 0);
    CHECK(strlen(buf) == need);
    // Bus lines carry level only: no edges, no hit.
    CHECK(strstr(buf, "{\"gpio\":4,\"name\":\"TX\",\"role\":\"bus tx\","
                      "\"type\":\"uart\",\"mode\":\"output\",\"level\":0,"
                      "\"detail\":\"1200 baud 8E1\"}") != NULL);
    CHECK(strstr(buf, "\"hit\":true,\"edges\":3,\"idle_ms\":1200") != NULL);
    CHECK(strstr(buf, "\"raw\":2345,\"mv\":1820") != NULL);
    CHECK(strstr(buf, "\"buses\"") == NULL); // no capture active
    free(buf);
    return 0;
}

// ---- UART: baud 10000 so one bit is exactly 100 us ----

static int test_uart_valid_frame(void)
{
    // Byte 0x55, no parity, 1 stop. Unrelated gpio-5 edge must be ignored.
    bus_edge_t e[] = {
        E(4, 0, 100), E(5, 0, 150), E(4, 1, 200), E(4, 0, 300), E(4, 1, 400),
        E(4, 0, 500), E(4, 1, 600), E(4, 0, 700), E(4, 1, 800), E(4, 0, 900),
        E(4, 1, 1000),
    };
    bus_uart_cfg_t cfg = {.gpio = 4, .baud = 10000, .data_bits = 8,
                          .parity = BUS_PARITY_NONE, .stop_bits = 1};
    bus_uart_result_t r;
    bus_uart_analyze(e, sizeof(e) / sizeof(e[0]), &cfg, &r);
    CHECK(r.frames_ok == 1);
    CHECK(r.framing_errors == 0);
    CHECK(r.parity_errors == 0);
    return 0;
}

static int test_uart_framing_error(void)
{
    // Same frame but the stop bit stays low.
    bus_edge_t e[] = {
        E(4, 0, 100), E(4, 1, 200), E(4, 0, 300), E(4, 1, 400), E(4, 0, 500),
        E(4, 1, 600), E(4, 0, 700), E(4, 1, 800), E(4, 0, 900),
    };
    bus_uart_cfg_t cfg = {.gpio = 4, .baud = 10000, .data_bits = 8,
                          .parity = BUS_PARITY_NONE, .stop_bits = 1};
    bus_uart_result_t r;
    bus_uart_analyze(e, sizeof(e) / sizeof(e[0]), &cfg, &r);
    CHECK(r.frames_ok == 0);
    CHECK(r.framing_errors == 1);
    CHECK(r.parity_errors == 0);
    return 0;
}

static int test_uart_parity(void)
{
    // Byte 0x01 with even parity: data has one '1', parity bit is 1.
    bus_edge_t ok[] = {E(4, 0, 100), E(4, 1, 200), E(4, 0, 300), E(4, 1, 1000)};
    bus_uart_cfg_t cfg = {.gpio = 4, .baud = 10000, .data_bits = 8,
                          .parity = BUS_PARITY_EVEN, .stop_bits = 1};
    bus_uart_result_t r;
    bus_uart_analyze(ok, sizeof(ok) / sizeof(ok[0]), &cfg, &r);
    CHECK(r.frames_ok == 1);
    CHECK(r.framing_errors == 0);
    CHECK(r.parity_errors == 0);

    // Same byte, parity bit 0: odd ones -> parity error (stop still high).
    bus_edge_t bad[] = {E(4, 0, 100), E(4, 1, 200), E(4, 0, 300), E(4, 1, 1100)};
    bus_uart_analyze(bad, sizeof(bad) / sizeof(bad[0]), &cfg, &r);
    CHECK(r.frames_ok == 0);
    CHECK(r.framing_errors == 0);
    CHECK(r.parity_errors == 1);
    return 0;
}

static int test_uart_glitch_ignored(void)
{
    // 10 us spike: start-bit center reads high, not a frame.
    bus_edge_t e[] = {E(4, 0, 100), E(4, 1, 110)};
    bus_uart_cfg_t cfg = {.gpio = 4, .baud = 10000, .data_bits = 8,
                          .parity = BUS_PARITY_NONE, .stop_bits = 1};
    bus_uart_result_t r;
    bus_uart_analyze(e, sizeof(e) / sizeof(e[0]), &cfg, &r);
    CHECK(r.frames_ok == 0);
    CHECK(r.framing_errors == 0);
    CHECK(r.parity_errors == 0);
    return 0;
}

// ---- I2C: sda=1, scl=2, full clock edges (fall+rise) ----

static int test_i2c_write(void)
{
    // Write 0x90 (addr 0x48) + 0x00, both ACKed, STOP.
    bus_edge_t e[] = {
        E(1, 0, 5), E(2, 0, 6), E(1, 1, 8), E(2, 1, 10), E(2, 0, 15),
        E(1, 0, 16), E(2, 1, 20), E(2, 0, 25), E(2, 1, 30), E(2, 0, 35),
        E(1, 1, 36), E(2, 1, 40), E(2, 0, 45), E(1, 0, 46), E(2, 1, 50),
        E(2, 0, 55), E(2, 1, 60), E(2, 0, 65), E(2, 1, 70), E(2, 0, 75),
        E(2, 1, 80), E(2, 0, 85), E(2, 1, 90), E(2, 0, 95), E(2, 1, 100),
        E(2, 0, 105), E(2, 1, 110), E(2, 0, 115), E(2, 1, 120), E(2, 0, 125),
        E(2, 1, 130), E(2, 0, 135), E(2, 1, 140), E(2, 0, 145), E(2, 1, 150),
        E(2, 0, 155), E(2, 1, 160), E(2, 0, 165), E(2, 1, 170), E(2, 0, 175),
        E(2, 1, 180), E(2, 0, 185), E(1, 1, 187), E(1, 0, 189), E(2, 1, 191),
        E(1, 1, 193),
    };
    bus_i2c_result_t r;
    bus_i2c_analyze(e, sizeof(e) / sizeof(e[0]), 1, 2, &r);
    CHECK(r.transactions == 1);
    CHECK(r.malformed == 0);
    CHECK(r.nacks == 0);
    CHECK(r.n_addrs == 1);
    CHECK(r.addrs[0].addr == 0x48);
    CHECK(r.addrs[0].reads == 0);
    CHECK(r.addrs[0].writes == 1);
    return 0;
}

static int test_i2c_nack_then_idle(void)
{
    // Byte 0x90, slave leaves SDA high on the ACK clock, capture ends with
    // the bus idle: no STOP edge exists, the complete frame still counts.
    bus_edge_t e[] = {
        E(1, 0, 5), E(2, 0, 6), E(1, 1, 8), E(2, 1, 10), E(2, 0, 15),
        E(1, 0, 16), E(2, 1, 20), E(2, 0, 25), E(2, 1, 30), E(2, 0, 35),
        E(1, 1, 36), E(2, 1, 40), E(2, 0, 45), E(1, 0, 46), E(2, 1, 50),
        E(2, 0, 55), E(2, 1, 60), E(2, 0, 65), E(2, 1, 70), E(2, 0, 75),
        E(2, 1, 80), E(2, 0, 85), E(1, 1, 87), E(2, 1, 90),
    };
    bus_i2c_result_t r;
    bus_i2c_analyze(e, sizeof(e) / sizeof(e[0]), 1, 2, &r);
    CHECK(r.transactions == 1);
    CHECK(r.malformed == 0);
    CHECK(r.nacks == 1);
    CHECK(r.n_addrs == 1 && r.addrs[0].addr == 0x48);
    return 0;
}

static int test_i2c_malformed(void)
{
    // START, 3 clocks, STOP with a partial byte.
    bus_edge_t e[] = {
        E(1, 0, 5), E(2, 0, 6), E(2, 1, 10), E(2, 0, 15), E(2, 1, 20),
        E(2, 0, 25), E(2, 1, 40), E(1, 1, 42),
    };
    bus_i2c_result_t r;
    bus_i2c_analyze(e, sizeof(e) / sizeof(e[0]), 1, 2, &r);
    CHECK(r.transactions == 0);
    CHECK(r.malformed == 1);
    return 0;
}

static int test_i2c_repeated_start(void)
{
    // Write 0x90 + ACK, repeated START, read 0x91 + ACK, STOP. Both phases
    // count even though only the second one ends with STOP.
    bus_edge_t e[] = {
        E(1, 0, 5), E(2, 0, 6), E(1, 1, 8), E(2, 1, 10), E(2, 0, 15),
        E(1, 0, 16), E(2, 1, 20), E(2, 0, 25), E(2, 1, 30), E(2, 0, 35),
        E(1, 1, 36), E(2, 1, 40), E(2, 0, 45), E(1, 0, 46), E(2, 1, 50),
        E(2, 0, 55), E(2, 1, 60), E(2, 0, 65), E(2, 1, 70), E(2, 0, 75),
        E(2, 1, 80), E(2, 0, 85), E(2, 1, 90), E(2, 0, 95), E(1, 1, 97),
        E(2, 1, 99), E(1, 0, 101), E(2, 0, 102), E(1, 1, 105), E(2, 1, 110),
        E(2, 0, 115), E(1, 0, 116), E(2, 1, 120), E(2, 0, 125), E(2, 1, 130),
        E(2, 0, 135), E(1, 1, 136), E(2, 1, 140), E(2, 0, 145), E(1, 0, 146),
        E(2, 1, 150), E(2, 0, 155), E(2, 1, 160), E(2, 0, 165), E(2, 1, 170),
        E(2, 0, 175), E(1, 1, 176), E(2, 1, 180), E(2, 0, 185), E(1, 0, 186),
        E(2, 1, 190), E(2, 0, 195), E(1, 1, 196), E(1, 0, 197), E(2, 1, 198),
        E(1, 1, 199),
    };
    bus_i2c_result_t r;
    bus_i2c_analyze(e, sizeof(e) / sizeof(e[0]), 1, 2, &r);
    CHECK(r.transactions == 2);
    CHECK(r.malformed == 0);
    CHECK(r.nacks == 0);
    CHECK(r.n_addrs == 1);
    CHECK(r.addrs[0].addr == 0x48);
    CHECK(r.addrs[0].writes == 1);
    CHECK(r.addrs[0].reads == 1);
    return 0;
}

// ---- SPI: sck=5, cs=6 (active low) ----

static int test_spi_transfer_and_runt(void)
{
    bus_edge_t e[] = {
        E(6, 0, 5), E(5, 1, 10), E(5, 0, 12), E(5, 1, 20), E(5, 0, 22),
        E(5, 1, 30), E(5, 0, 32), E(5, 1, 40), E(5, 0, 42), E(5, 1, 50),
        E(5, 0, 52), E(5, 1, 60), E(5, 0, 62), E(5, 1, 70), E(5, 0, 72),
        E(5, 1, 80), E(6, 1, 90),
        E(6, 0, 100), E(5, 1, 110), E(5, 0, 112), E(5, 1, 120), E(5, 0, 122),
        E(5, 1, 130), E(6, 1, 140),
        E(6, 0, 150), E(6, 1, 160),
    };
    bus_spi_result_t r;
    bus_spi_analyze(e, sizeof(e) / sizeof(e[0]), 5, 6, &r);
    CHECK(r.transfers == 1); // 8 clocks
    CHECK(r.bytes == 1);
    CHECK(r.runts == 1); // 3 clocks
    // CS-only blip and stray clocks are ignored, not errors.
    return 0;
}

static int test_bus_status(void)
{
    bus_run_t run = {0};
    CHECK(strcmp(bus_status_str(&run, 5000, 10000), "no-traffic") == 0);
    run.frame_errors = 2;
    run.last_error_ms = 9900;
    CHECK(strcmp(bus_status_str(&run, 5000, 10000), "error") == 0);
    // A valid frame resets the streak: fresh traffic wins over history.
    run.frames_ok = 1;
    run.ever_ok = true;
    run.last_ok_ms = 9900;
    run.consec_errors = 0;
    CHECK(strcmp(bus_status_str(&run, 5000, 10000), "valid") == 0);
    // But 3+ consecutive recent errors flip it back to error.
    run.consec_errors = 5;
    run.last_error_ms = 9950;
    CHECK(strcmp(bus_status_str(&run, 5000, 10000), "error") == 0);
    // Old errors with no recent traffic: stale, not error.
    CHECK(strcmp(bus_status_str(&run, 5000, 20000), "stale") == 0);
    CHECK(strcmp(bus_status_str(&run, 0, 20000), "stale") == 0); // default 5 s
    return 0;
}

static int test_bus_payload(void)
{
    bus_desc_t descs[2] = {
        {.name = "WALLBUS", .type = BUS_UART, .rx_gpio = 16, .baud = 1200,
         .detail = "1200 baud 8E1"},
        {.name = "SENSORS", .type = BUS_I2C, .sda_gpio = 21, .scl_gpio = 22},
    };
    bus_run_t runs[2] = {0};
    runs[0].frames_ok = 128;
    runs[0].frame_errors = 44;
    runs[0].parity_errors = 4;
    runs[0].ever_ok = true;
    runs[0].last_ok_ms = 9600;
    runs[0].last_error_ms = 9900;
    runs[1].frame_errors = 1;
    runs[1].last_error_ms = 9000;
    size_t need = gpio_state_format(NULL, 0, NULL, NULL, 0, descs, runs, 2, 10000);
    char *buf = malloc(need + 1);
    CHECK(buf != NULL);
    gpio_state_format(buf, need + 1, NULL, NULL, 0, descs, runs, 2, 10000);
    CHECK(strlen(buf) == need);
    CHECK(strstr(buf, "\"buses\":[{\"name\":\"WALLBUS\",\"type\":\"uart\","
                      "\"status\":\"valid\",\"frames_ok\":128,"
                      "\"frame_errors\":44,\"idle_ms\":400,"
                      "\"error_idle_ms\":100,\"parity_errors\":4,"
                      "\"detail\":\"1200 baud 8E1\"}") != NULL);
    CHECK(strstr(buf, "\"name\":\"SENSORS\",\"type\":\"i2c\",\"status\":\"error\","
                      "\"frames_ok\":0,\"frame_errors\":1,\"error_idle_ms\":1000,"
                      "\"nacks\":0,\"addresses\":[]") != NULL);
    free(buf);
    return 0;
}

static int test_run_accumulate(void)
{
    bus_run_t run = {0};
    bus_uart_result_t u = {.frames_ok = 3, .framing_errors = 1};
    bus_run_add_uart(&run, &u, 5000);
    CHECK(run.frames_ok == 3 && run.frame_errors == 1);
    CHECK(run.ever_ok && run.last_ok_ms == 5000);
    CHECK(run.consec_errors == 1); // valid batch resets, then adds its error
    CHECK(run.last_error_ms == 5000);
    bus_uart_result_t u2 = {.frames_ok = 0, .framing_errors = 2};
    bus_run_add_uart(&run, &u2, 6000);
    CHECK(run.frames_ok == 3 && run.frame_errors == 3);
    CHECK(run.last_ok_ms == 5000); // no new valid frame: timestamp kept
    CHECK(run.consec_errors == 3);
    CHECK(run.last_error_ms == 6000);

    bus_i2c_result_t i = {0};
    i.transactions = 1;
    i.addrs[0].addr = 0x48;
    i.addrs[0].writes = 1;
    i.n_addrs = 1;
    bus_run_add_i2c(&run, &i, 7000);
    CHECK(run.frames_ok == 4);
    CHECK(run.n_addrs == 1 && run.addrs[0].writes == 1);
    bus_run_add_i2c(&run, &i, 8000); // same addr merges
    CHECK(run.n_addrs == 1 && run.addrs[0].writes == 2);
    return 0;
}

static int test_bus_desc_usable(void)
{
    bus_desc_t uart_ok = {.name = "WALLBUS", .type = BUS_UART, .rx_gpio = 16, .baud = 1200};
    CHECK(bus_desc_usable(&uart_ok));
    bus_desc_t uart_no_baud = {.name = "WALLBUS", .type = BUS_UART, .rx_gpio = 16, .baud = 0};
    CHECK(!bus_desc_usable(&uart_no_baud));
    bus_desc_t uart_no_pin = {.name = "WALLBUS", .type = BUS_UART, .rx_gpio = -1, .baud = 1200};
    CHECK(!bus_desc_usable(&uart_no_pin));
    bus_desc_t no_name = {.name = NULL, .type = BUS_UART, .rx_gpio = 16, .baud = 1200};
    CHECK(!bus_desc_usable(&no_name));
    bus_desc_t i2c_ok = {.name = "SENS", .type = BUS_I2C, .sda_gpio = 21, .scl_gpio = 22};
    CHECK(bus_desc_usable(&i2c_ok));
    bus_desc_t i2c_same = {.name = "SENS", .type = BUS_I2C, .sda_gpio = 21, .scl_gpio = 21};
    CHECK(!bus_desc_usable(&i2c_same));
    bus_desc_t spi_ok = {.name = "FLASH", .type = BUS_SPI, .sck_gpio = 18, .cs_gpio = 5};
    CHECK(bus_desc_usable(&spi_ok));
    bus_desc_t spi_same = {.name = "FLASH", .type = BUS_SPI, .sck_gpio = 18, .cs_gpio = 18};
    CHECK(!bus_desc_usable(&spi_same));
    return 0;
}

static int test_bus_desc_gpios(void)
{
    bus_desc_t buses[3] = {
        {.name = "WALLBUS", .type = BUS_UART, .rx_gpio = 16},
        {.name = "SENS", .type = BUS_I2C, .sda_gpio = 21, .scl_gpio = 22},
        {.name = "AUX", .type = BUS_SPI, .sck_gpio = -1, .cs_gpio = 5},
    };
    int8_t out[8];
    // A negative pin is skipped individually, the other line is still kept.
    size_t n = bus_desc_gpios(buses, 3, out, 8);
    CHECK(n == 4);
    CHECK(out[0] == 16 && out[1] == 21 && out[2] == 22 && out[3] == 5);
    bus_desc_t dup[2] = {
        {.name = "A", .type = BUS_UART, .rx_gpio = 16},
        {.name = "B", .type = BUS_UART, .rx_gpio = 16},
    };
    CHECK(bus_desc_gpios(dup, 2, out, 8) == 1);
    CHECK(out[0] == 16);
    CHECK(bus_desc_gpios(buses, 3, out, 2) == 2); // capped at max
    return 0;
}

int main(void)
{
    if (test_type_strings())
        return 1;
    if (test_hit_logic())
        return 1;
    if (test_pin_payload())
        return 1;
    if (test_uart_valid_frame())
        return 1;
    if (test_uart_framing_error())
        return 1;
    if (test_uart_parity())
        return 1;
    if (test_uart_glitch_ignored())
        return 1;
    if (test_i2c_write())
        return 1;
    if (test_i2c_nack_then_idle())
        return 1;
    if (test_i2c_malformed())
        return 1;
    if (test_i2c_repeated_start())
        return 1;
    if (test_spi_transfer_and_runt())
        return 1;
    if (test_bus_status())
        return 1;
    if (test_bus_payload())
        return 1;
    if (test_run_accumulate())
        return 1;
    if (test_bus_desc_usable())
        return 1;
    if (test_bus_desc_gpios())
        return 1;
    printf("test_pin_inspector: all tests passed\n");
    return 0;
}
