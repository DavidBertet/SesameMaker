// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Generic pin + bus inspector: the app declares WHAT to watch (pin and bus
// descriptors — wiring info only, no protocol knowledge), core samples,
// captures and validates. Split so other projects reuse it by only writing
// providers:
//
//   size_t my_pins(pin_desc_t *out, size_t max) { ... }   // levels, ADC
//   size_t my_buses(bus_desc_t *out, size_t max) {        // framing checks
//       out[0] = (bus_desc_t){ .name = "WALLBUS", .type = BUS_UART,
//           .rx_gpio = 16, .baud = 1200, .data_bits = 8,
//           .parity = BUS_PARITY_EVEN, .stop_bits = 1,
//           .detail = "1200 baud 8E1" };
//       return 1;
//   }
//
// Two layers:
//   pins: instantaneous level / ADC / edge counters (5 ms sampler task).
//   buses: on-demand edge capture (GPIO ISR timestamps) + framing analysis:
//     UART: start-bit fall, data/parity sampled at bit centers, stop bit(s)
//       must read high. I2C: START/STOP conditions, 9-bit groups, address
//       list. SPI: CS-bounded clock bursts, byte multiples. Payload bytes
//       are NEVER interpreted beyond the I2C address (framing, not app data).
//
// No ESP-IDF includes here so formatting + analysis stay host-testable; the
// ISR capture and ADC/gpio sampling live in bus_capture.c / pins_adc.c /
// pins_sampler.c next to it in this folder.
//
// Wire payload: {"type":"gpio_state","pins":[...],"buses":[...]}:
//   pins: digital -> level[,hit][,edges[,idle_ms]]; analog -> [raw,mv];
//         bus lines -> level only (+detail).
//   buses: name,type,status,frames_ok,frame_errors[,idle_ms][,detail]
//     (+parity_errors for uart, +addresses[]/nacks for i2c,
//      +error_idle_ms once an error was seen). status is one of
//     "no-traffic" | "valid" | "stale" | "error".

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIN_INSPECTOR_MAX 16
#define BUS_INSPECTOR_MAX 4
#define BUS_I2C_MAX_ADDRS 8
#define BUS_STALE_DEFAULT_MS 5000

// ==== Pins ====

typedef enum
{
    PIN_TYPE_DIGITAL = 0, // plain GPIO: level, optional hit, edge counters
    PIN_TYPE_ANALOG,      // ADC oneshot: raw + calibrated mV
    PIN_TYPE_UART,        // bus line: level only, validated via buses[]
    PIN_TYPE_I2C,         // bus line: level only, validated via buses[]
    PIN_TYPE_SPI,         // bus line: level only, validated via buses[]
} pin_type_t;

typedef struct
{
    int8_t gpio;       // <0 (or name NULL) = slot skipped
    const char *name;  // short id, e.g. "RELAY"
    const char *role;  // human role, e.g. "relay driver"
    pin_type_t type;   // how to sample + display
    bool is_output;    // mode display; hit is only computed for inputs
    bool active_low;   // digital inputs: level 0 counts as hit
    bool report_hit;   // digital inputs: include "hit" (else level only)
    int adc_atten;     // analog: 0..3 = ADC_ATTEN_DB_0..DB_12, else default DB_12
    const char *detail; // optional static info, e.g. "open-collector"
} pin_desc_t;

typedef struct
{
    int level; // digital/bus: 0/1
    bool has_hit;
    bool hit;
    bool has_analog;
    int raw;
    int mv;
    // Edge activity (digital only): counted by the background sampler so the
    // UI sees bursts a ~2 s poll would miss.
    uint32_t edges;   // transitions seen since boot
    bool has_activity; // ever saw an edge (idle_ms valid)
    uint32_t idle_ms; // ms since the last edge, at sample time
} pin_sample_t;

// App table callback: fill up to max descriptors, return the count. Called on
// every sample so runtime-reconfigurable pins (NVS, menus) stay fresh; keep it
// cheap (read cached config, no I/O).
typedef size_t (*pin_provider_fn)(pin_desc_t *out, size_t max);

void pin_inspector_set_provider(pin_provider_fn fn);
pin_provider_fn pin_inspector_get_provider(void);
const char *pin_type_str(pin_type_t t);
// Normalized hit for a digital input level (active-level applied).
bool pin_hit_of(int level, bool active_low);

// ==== Buses (framing validation, no payload decoding) ====

typedef enum
{
    BUS_UART = 0,
    BUS_I2C,
    BUS_SPI,
} bus_type_t;

#define BUS_PARITY_NONE 0
#define BUS_PARITY_EVEN 1
#define BUS_PARITY_ODD 2

typedef struct
{
    const char *name; // e.g. "WALLBUS" (NULL = slot skipped)
    bus_type_t type;
    int8_t rx_gpio;  // uart: watched line
    int8_t sda_gpio; // i2c
    int8_t scl_gpio; // i2c
    int8_t sck_gpio; // spi
    int8_t cs_gpio;  // spi, active low
    int baud;        // uart
    uint8_t data_bits; // uart: 5..9 (0 = default 8)
    uint8_t parity;    // uart: BUS_PARITY_* (default NONE)
    uint8_t stop_bits; // uart: 1..2 (0 = default 1)
    uint32_t stale_after_ms; // no valid frame this long -> "stale" (0 = default)
    const char *detail; // e.g. "1200 baud 8E1"
} bus_desc_t;

typedef size_t (*bus_provider_fn)(bus_desc_t *out, size_t max);
// Runtime totals are indexed by table position: return entries in stable
// order across calls (a changed table restarts the totals).

void pin_inspector_set_bus_provider(bus_provider_fn fn);
bus_provider_fn pin_inspector_get_bus_provider(void);
const char *bus_type_str(bus_type_t t);

// ---- Pure bus-table helpers (host-testable; used by the IDF capture session) ----

// A bus entry is usable when its watched lines are declared (and distinct).
bool bus_desc_usable(const bus_desc_t *d);
// Collect the watched gpios of bus entries (deduped, negative pins skipped).
// Returns the count.
size_t bus_desc_gpios(const bus_desc_t *b, size_t nb, int8_t *out, size_t max);

// Captured edge: level AFTER the transition. Timestamps are µs (uint32 wraps
// every ~71 min; compare with bus_dt_us).
typedef struct
{
    uint8_t gpio;
    uint8_t level;
    uint32_t t_us;
} bus_edge_t;

// Wrap-safe microsecond diff.
static inline int32_t bus_dt_us(uint32_t later, uint32_t earlier)
{
    return (int32_t)(later - earlier);
}

// ---- Pure analysis over an edge window (host-testable) ----

typedef struct
{
    int8_t gpio;
    int baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
} bus_uart_cfg_t;

typedef struct
{
    uint32_t frames_ok;
    uint32_t framing_errors; // stop bit(s) read low: collisions, breaks, garbage
    uint32_t parity_errors;  // parity mismatch with stop bit ok: wrong baud,
                             // 8N1 traffic on an 8E1 bus, marginal timing
} bus_uart_result_t;

// UART framing: falling edge = start-bit candidate, data/parity sampled at
// bit centers, stop bit(s) must read high. Assumes idle-high before the
// first edge. Bytes are counted, never reported.
void bus_uart_analyze(const bus_edge_t *e, size_t n, const bus_uart_cfg_t *cfg,
                      bus_uart_result_t *r);

typedef struct
{
    uint8_t addr; // 7-bit
    uint32_t reads;
    uint32_t writes;
} bus_i2c_addr_t;

typedef struct
{
    uint32_t transactions; // complete START..STOP frames, byte-aligned
    uint32_t malformed;    // STOP with partial byte, or bytes outside START
    uint32_t nacks;        // NACKed 9th bits (framing ok, device said no)
    bus_i2c_addr_t addrs[BUS_I2C_MAX_ADDRS];
    uint8_t n_addrs;
} bus_i2c_result_t;

// I2C framing: START/STOP conditions, 9-bit groups (8 data + ACK), address
// list. Assumes idle-high (pulled up) before the first edge. Edges before
// the first START are ignored (capture started mid-transaction).
void bus_i2c_analyze(const bus_edge_t *e, size_t n, int8_t sda_gpio,
                     int8_t scl_gpio, bus_i2c_result_t *r);

typedef struct
{
    uint32_t transfers; // CS assertions with byte-multiple clocks
    uint32_t bytes;     // counted clock bytes
    uint32_t runts;     // CS assertions with non-multiple-of-8 clocks
} bus_spi_result_t;

// SPI framing: CS-bounded (active-low) rising-edge clock bursts. CPOL/CPHA
// don't matter for clock counting; MOSI/MISO are not sampled.
void bus_spi_analyze(const bus_edge_t *e, size_t n, int8_t sck_gpio,
                     int8_t cs_gpio, bus_spi_result_t *r);

// ---- Cumulative per-bus runtime (owned by the capture session) ----

typedef struct
{
    uint32_t frames_ok;   // uart frames / i2c transactions / spi transfers
    uint32_t frame_errors; // ... of which framing-level failures
    uint32_t parity_errors; // uart only: parity mismatches (subset of errors)
    uint32_t nacks;        // i2c only
    bus_i2c_addr_t addrs[BUS_I2C_MAX_ADDRS];
    uint8_t n_addrs;
    bool ever_ok;
    uint32_t last_ok_ms;
    uint32_t consec_errors; // errors since the last valid frame (recency)
    uint32_t last_error_ms; // valid when frame_errors > 0
} bus_run_t;

void bus_run_add_uart(bus_run_t *run, const bus_uart_result_t *r, uint32_t now_ms);
void bus_run_add_i2c(bus_run_t *run, const bus_i2c_result_t *r, uint32_t now_ms);
void bus_run_add_spi(bus_run_t *run, const bus_spi_result_t *r, uint32_t now_ms);

// no-traffic: nothing seen yet. error: only errors so far, or 3+ consecutive
// errors recently (a valid frame resets the streak). valid: a valid frame
// within stale_after_ms. stale: valid frames before, none recently.
const char *bus_status_str(const bus_run_t *run, uint32_t stale_after_ms,
                           uint32_t now_ms);

// Format the full gpio_state payload (pins + buses). NULL buf returns the
// needed size (excl NUL), mirroring raw_json.c. buses may be NULL/0 (field
// omitted when no capture is active).
size_t gpio_state_format(char *buf, size_t len,
                         const pin_desc_t *pdescs, const pin_sample_t *samples,
                         size_t npins, const bus_desc_t *bdescs,
                         const bus_run_t *runs, size_t nbuses, uint32_t now_ms);
