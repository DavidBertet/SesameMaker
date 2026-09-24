// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "pin_inspector.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static pin_provider_fn s_provider;
static bus_provider_fn s_bus_provider;

void pin_inspector_set_provider(pin_provider_fn fn)
{
    s_provider = fn;
}

pin_provider_fn pin_inspector_get_provider(void)
{
    return s_provider;
}

void pin_inspector_set_bus_provider(bus_provider_fn fn)
{
    s_bus_provider = fn;
}

bus_provider_fn pin_inspector_get_bus_provider(void)
{
    return s_bus_provider;
}

const char *pin_type_str(pin_type_t t)
{
    switch (t)
    {
    case PIN_TYPE_ANALOG:
        return "analog";
    case PIN_TYPE_UART:
        return "uart";
    case PIN_TYPE_I2C:
        return "i2c";
    case PIN_TYPE_SPI:
        return "spi";
    case PIN_TYPE_DIGITAL:
    default:
        return "digital";
    }
}

const char *bus_type_str(bus_type_t t)
{
    switch (t)
    {
    case BUS_I2C:
        return "i2c";
    case BUS_SPI:
        return "spi";
    case BUS_UART:
    default:
        return "uart";
    }
}

bool pin_hit_of(int level, bool active_low)
{
    bool high = level != 0;
    return active_low ? !high : high;
}

// Same sizing-writer idiom as raw_json.c: tracks the logical offset, never
// writes past buf[size-1], writes nothing when buf is NULL (for sizing).
typedef struct
{
    char *buf;
    size_t size;
    size_t off;
} writer_t;

static void w_out(writer_t *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
    {
        return;
    }
    if (!w->buf || w->off >= w->size)
    {
        w->off += (size_t)need;
        return;
    }
    va_start(ap, fmt);
    vsnprintf(w->buf + w->off, w->size - w->off, fmt, ap);
    va_end(ap);
    w->off += (size_t)need;
}

// ==== Pure bus-table helpers (used by the IDF capture session) ====

bool bus_desc_usable(const bus_desc_t *d)
{
    if (!d->name)
    {
        return false;
    }
    switch (d->type)
    {
    case BUS_UART:
        return d->rx_gpio >= 0 && d->baud > 0;
    case BUS_I2C:
        return d->sda_gpio >= 0 && d->scl_gpio >= 0 &&
               d->sda_gpio != d->scl_gpio;
    case BUS_SPI:
        return d->sck_gpio >= 0 && d->cs_gpio >= 0 &&
               d->sck_gpio != d->cs_gpio;
    }
    return false;
}

size_t bus_desc_gpios(const bus_desc_t *b, size_t nb, int8_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < nb; i++)
    {
        int8_t pins[2];
        size_t np = 0;
        switch (b[i].type)
        {
        case BUS_UART:
            pins[0] = b[i].rx_gpio;
            np = 1;
            break;
        case BUS_I2C:
            pins[0] = b[i].sda_gpio;
            pins[1] = b[i].scl_gpio;
            np = 2;
            break;
        case BUS_SPI:
            pins[0] = b[i].sck_gpio;
            pins[1] = b[i].cs_gpio;
            np = 2;
            break;
        }
        for (size_t k = 0; k < np; k++)
        {
            if (pins[k] < 0)
            {
                continue;
            }
            bool dup = false;
            for (size_t j = 0; j < n; j++)
            {
                if (out[j] == pins[k])
                {
                    dup = true;
                    break;
                }
            }
            if (!dup && n < max)
            {
                out[n++] = pins[k];
            }
        }
    }
    return n;
}

// ==== UART framing analysis ====

// Line level at time t from edge history (idle-high before the first edge).
// *pos/*level carry the replay state across calls with increasing t.
static int uart_level_at(const bus_edge_t *e, size_t n, size_t *pos,
                         uint8_t gpio, uint32_t t, int *level)
{
    while (*pos < n && bus_dt_us(e[*pos].t_us, t) <= 0)
    {
        if (e[*pos].gpio == gpio)
        {
            *level = e[*pos].level ? 1 : 0;
        }
        (*pos)++;
    }
    return *level;
}

void bus_uart_analyze(const bus_edge_t *e, size_t n, const bus_uart_cfg_t *cfg,
                      bus_uart_result_t *r)
{
    r->frames_ok = 0;
    r->framing_errors = 0;
    r->parity_errors = 0;
    if (!cfg || cfg->baud <= 0)
    {
        return;
    }
    uint8_t db = (cfg->data_bits >= 5 && cfg->data_bits <= 9) ? cfg->data_bits : 8;
    uint8_t sb = (cfg->stop_bits == 2) ? 2 : 1;
    uint8_t par = (cfg->parity <= BUS_PARITY_ODD) ? cfg->parity : BUS_PARITY_NONE;
    uint32_t bit = 1000000u / (uint32_t)cfg->baud;
    if (bit == 0)
    {
        return;
    }
    uint8_t gpio = cfg->gpio;
    uint32_t frame = (uint32_t)(1 + db + (par ? 1 : 0) + sb) * bit;
    size_t i = 0;
    size_t pos = 0;
    int level = 1;
    int prev = 1; // idle high
    while (i < n)
    {
        if (e[i].gpio != gpio)
        {
            i++;
            continue;
        }
        int lv = e[i].level ? 1 : 0;
        if (lv == 0 && prev == 1)
        {
            // Start-bit candidate: replay state starts at the falling edge.
            uint32_t t0 = e[i].t_us;
            pos = i + 1;
            level = 0;
            bool framing_err = false;
            bool parity_err = false;
            if (uart_level_at(e, n, &pos, gpio, t0 + bit / 2, &level) != 0)
            {
                prev = lv; // glitch, not a start bit
                i++;
                continue;
            }
            uint32_t v = 0;
            for (uint8_t k = 0; k < db; k++)
            {
                if (uart_level_at(e, n, &pos, gpio,
                                  t0 + bit + bit / 2 + (uint32_t)k * bit,
                                  &level))
                {
                    v |= 1u << k;
                }
            }
            if (par)
            {
                int p = uart_level_at(e, n, &pos, gpio,
                                      t0 + bit + bit / 2 + (uint32_t)db * bit,
                                      &level);
                unsigned ones = (unsigned)p;
                for (uint8_t k = 0; k < db; k++)
                {
                    ones += (v >> k) & 1u;
                }
                if ((par == BUS_PARITY_EVEN && (ones & 1u)) ||
                    (par == BUS_PARITY_ODD && !(ones & 1u)))
                {
                    parity_err = true;
                }
            }
            for (uint8_t s = 0; s < sb; s++)
            {
                uint32_t ts = t0 + bit + bit / 2 +
                              (uint32_t)(db + (par ? 1 : 0) + s) * bit;
                if (uart_level_at(e, n, &pos, gpio, ts, &level) != 1)
                {
                    framing_err = true;
                }
            }
            if (framing_err)
            {
                r->framing_errors++;
            }
            else if (parity_err)
            {
                r->parity_errors++;
            }
            else
            {
                r->frames_ok++;
            }
            // Skip past this frame: data-bit falls are not new starts. The
            // stop bit guarantees ≥1 bit of high after a clean frame, so a
            // strict `<` never eats the next start (only its own tail).
            uint32_t tend = t0 + frame;
            while (i < n && bus_dt_us(e[i].t_us, tend) < 0)
            {
                if (e[i].gpio == gpio)
                {
                    prev = e[i].level ? 1 : 0;
                }
                i++;
            }
            continue;
        }
        prev = lv;
        i++;
    }
}

// ==== I2C framing analysis ====

static void i2c_record_addr(bus_i2c_result_t *r, uint8_t byte)
{
    uint8_t addr = (uint8_t)(byte >> 1);
    bool read = (byte & 1u) != 0;
    for (uint8_t k = 0; k < r->n_addrs; k++)
    {
        if (r->addrs[k].addr == addr)
        {
            if (read)
            {
                r->addrs[k].reads++;
            }
            else
            {
                r->addrs[k].writes++;
            }
            return;
        }
    }
    if (r->n_addrs < BUS_I2C_MAX_ADDRS)
    {
        r->addrs[r->n_addrs].addr = addr;
        r->addrs[r->n_addrs].reads = read ? 1 : 0;
        r->addrs[r->n_addrs].writes = read ? 0 : 1;
        r->n_addrs++;
    }
    // Beyond the cap the transaction still counts, the address is dropped.
}

void bus_i2c_analyze(const bus_edge_t *e, size_t n, int8_t sda_gpio,
                     int8_t scl_gpio, bus_i2c_result_t *r)
{
    memset(r, 0, sizeof(*r));
    int sda = 1, scl = 1; // pulled up
    bool in_txn = false;
    uint32_t tbits = 0; // bits (data + ACK slots) in the open transaction
    uint32_t cur = 0;
    uint8_t first_byte = 0;
    uint32_t nbytes = 0;
    for (size_t i = 0; i < n; i++)
    {
        int lv = e[i].level ? 1 : 0;
        if (e[i].gpio == (uint8_t)sda_gpio)
        {
            if (scl == 1)
            {
                if (lv == 0)
                {
                    // START (or repeated START): SDA falls while SCL high.
                    if (in_txn)
                    {
                        if ((tbits % 9u) != 0)
                        {
                            r->malformed++; // previous frame left hanging
                        }
                        else if (nbytes >= 1)
                        {
                            // Repeated START after a complete phase (the
                            // classic write-register then read): the phase
                            // stands on its own, STOP or not.
                            r->transactions++;
                            i2c_record_addr(r, first_byte);
                        }
                    }
                    in_txn = true;
                    tbits = 0;
                    nbytes = 0;
                }
                else if (in_txn)
                {
                    // STOP: SDA rises while SCL high.
                    if ((tbits % 9u) != 0)
                    {
                        r->malformed++;
                    }
                    else if (nbytes >= 1)
                    {
                        r->transactions++;
                        i2c_record_addr(r, first_byte);
                    }
                    // Empty (tbits == 0, nbytes == 0) is a bare
                    // START..STOP: ignore, not an error.
                    in_txn = false;
                }
            }
            sda = lv;
        }
        else if (e[i].gpio == (uint8_t)scl_gpio)
        {
            if (lv == 1)
            {
                // Rising clock — with one exception: right after a complete
                // frame the master raises SCL once to set up the STOP (SDA
                // low) or repeated START (SDA high) that follows. That setup
                // rise is not a data clock. Peek at the next SDA/SCL edge:
                // only the matching condition confirms it, anything else
                // (or nothing) means a real clock.
                bool setup_rise = false;
                if (in_txn && tbits > 0 && (tbits % 9u) == 0)
                {
                    size_t j = i + 1;
                    while (j < n && e[j].gpio != (uint8_t)sda_gpio &&
                           e[j].gpio != (uint8_t)scl_gpio)
                    {
                        j++;
                    }
                    if (j < n && e[j].gpio == (uint8_t)sda_gpio)
                    {
                        int jlv = e[j].level ? 1 : 0;
                        if ((sda == 0 && jlv == 1) || (sda == 1 && jlv == 0))
                        {
                            setup_rise = true;
                        }
                    }
                }
                if (!setup_rise && in_txn)
                {
                    // Sample SDA (data bits MSB-first, 9th slot is ACK).
                    if ((tbits % 9u) < 8u)
                    {
                        cur = (cur << 1) | (uint32_t)sda;
                        if ((tbits % 9u) == 7u)
                        {
                            if (nbytes == 0)
                            {
                                first_byte = (uint8_t)cur;
                            }
                            nbytes++;
                        }
                    }
                    else if (sda == 1)
                    {
                        r->nacks++;
                    }
                    tbits++;
                }
                // Stray clocks outside a transaction (capture started
                // mid-way) and setup rises are swallowed silently.
            }
            scl = lv;
        }
    }
    // Capture ending with a byte-complete transaction but no STOP edge is
    // normal after a NACK (the line never fell, so no rise exists): the
    // master considers it done, count it. A partial trailing byte is
    // genuinely incomplete: ignore.
    if (in_txn && (tbits % 9u) == 0 && nbytes >= 1)
    {
        r->transactions++;
        i2c_record_addr(r, first_byte);
    }
}

// ==== SPI framing analysis (CS-bounded rising-edge clocks) ====

void bus_spi_analyze(const bus_edge_t *e, size_t n, int8_t sck_gpio,
                     int8_t cs_gpio, bus_spi_result_t *r)
{
    r->transfers = 0;
    r->bytes = 0;
    r->runts = 0;
    bool open = false;
    uint32_t clocks = 0;
    for (size_t i = 0; i < n; i++)
    {
        int lv = e[i].level ? 1 : 0;
        if (e[i].gpio == (uint8_t)cs_gpio)
        {
            if (lv == 0)
            {
                open = true; // CS asserted (active low)
                clocks = 0;
            }
            else if (open)
            {
                if (clocks > 0 && (clocks % 8u) == 0)
                {
                    r->transfers++;
                    r->bytes += clocks / 8u;
                }
                else if (clocks > 0)
                {
                    r->runts++;
                }
                open = false;
            }
        }
        else if (e[i].gpio == (uint8_t)sck_gpio)
        {
            if (lv == 1 && open)
            {
                clocks++;
            }
        }
    }
    // Capture ending with CS asserted is incomplete data, not an error.
}

// ==== Runtime accumulation + status + payload ====

// A batch with valid frames resets the consecutive-error streak, then adds
// its own errors; a batch with nothing new leaves both alone.
static void run_batch(bus_run_t *run, uint32_t ok, uint32_t errors,
                      uint32_t now_ms)
{
    if (ok > 0)
    {
        run->ever_ok = true;
        run->last_ok_ms = now_ms;
        run->consec_errors = 0;
    }
    if (errors > 0)
    {
        run->frame_errors += errors;
        run->consec_errors += errors;
        run->last_error_ms = now_ms;
    }
}

void bus_run_add_uart(bus_run_t *run, const bus_uart_result_t *r, uint32_t now_ms)
{
    run->frames_ok += r->frames_ok;
    run->parity_errors += r->parity_errors;
    run_batch(run, r->frames_ok, r->framing_errors + r->parity_errors, now_ms);
}

void bus_run_add_i2c(bus_run_t *run, const bus_i2c_result_t *r, uint32_t now_ms)
{
    run->frames_ok += r->transactions;
    run->nacks += r->nacks;
    for (uint8_t k = 0; k < r->n_addrs; k++)
    {
        bool found = false;
        for (uint8_t j = 0; j < run->n_addrs; j++)
        {
            if (run->addrs[j].addr == r->addrs[k].addr)
            {
                run->addrs[j].reads += r->addrs[k].reads;
                run->addrs[j].writes += r->addrs[k].writes;
                found = true;
                break;
            }
        }
        if (!found && run->n_addrs < BUS_I2C_MAX_ADDRS)
        {
            run->addrs[run->n_addrs++] = r->addrs[k];
        }
    }
    // NACKs are framing-valid activity (the master is alive), so they mark
    // freshness without counting as errors.
    if (r->transactions > 0 || r->nacks > 0)
    {
        run->ever_ok = true;
        run->last_ok_ms = now_ms;
    }
    if (r->malformed > 0)
    {
        run->frame_errors += r->malformed;
        run->consec_errors += r->malformed;
        run->last_error_ms = now_ms;
    }
    else if (r->transactions > 0)
    {
        run->consec_errors = 0;
    }
}

void bus_run_add_spi(bus_run_t *run, const bus_spi_result_t *r, uint32_t now_ms)
{
    run->frames_ok += r->transfers;
    run_batch(run, r->transfers, r->runts, now_ms);
}

#define BUS_CONSEC_ERROR_LIMIT 3

const char *bus_status_str(const bus_run_t *run, uint32_t stale_after_ms,
                           uint32_t now_ms)
{
    uint32_t stale = stale_after_ms ? stale_after_ms : BUS_STALE_DEFAULT_MS;
    // Signed subtractions: immune to the 49-day uint32 wrap.
    bool fresh = run->ever_ok &&
                 (int32_t)(now_ms - run->last_ok_ms) <= (int32_t)stale;
    bool errors_recent = run->frame_errors > 0 &&
                         (int32_t)(now_ms - run->last_error_ms) <= (int32_t)stale;
    if (!run->ever_ok && run->frame_errors == 0)
    {
        return "no-traffic";
    }
    if (!run->ever_ok)
    {
        return "error"; // only garbage so far
    }
    if (errors_recent && run->consec_errors >= BUS_CONSEC_ERROR_LIMIT)
    {
        return "error"; // valid traffic exists, but errors keep coming
    }
    if (fresh)
    {
        return "valid";
    }
    return "stale";
}

static void emit_pin(writer_t *w, const pin_desc_t *d, const pin_sample_t *s)
{
    w_out(w, "{\"gpio\":%d,\"name\":\"%s\",\"role\":\"%s\",\"type\":\"%s\","
             "\"mode\":\"%s\"",
          d->gpio, d->name, d->role ? d->role : "",
          pin_type_str(d->type), d->is_output ? "output" : "input");
    if (d->type == PIN_TYPE_ANALOG)
    {
        if (s->has_analog)
        {
            w_out(w, ",\"raw\":%d,\"mv\":%d", s->raw, s->mv);
        }
    }
    else
    {
        w_out(w, ",\"level\":%d", s->level ? 1 : 0);
        if (d->type == PIN_TYPE_DIGITAL)
        {
            if (s->has_hit)
            {
                w_out(w, ",\"hit\":%s", s->hit ? "true" : "false");
            }
            w_out(w, ",\"edges\":%lu", (unsigned long)s->edges);
            if (s->has_activity)
            {
                w_out(w, ",\"idle_ms\":%lu", (unsigned long)s->idle_ms);
            }
        }
    }
    if (d->detail)
    {
        w_out(w, ",\"detail\":\"%s\"", d->detail);
    }
    w_out(w, "}");
}

static void emit_bus(writer_t *w, const bus_desc_t *d, const bus_run_t *run,
                     uint32_t now_ms)
{
    uint32_t stale = d->stale_after_ms ? d->stale_after_ms : BUS_STALE_DEFAULT_MS;
    w_out(w, "{\"name\":\"%s\",\"type\":\"%s\",\"status\":\"%s\","
             "\"frames_ok\":%lu,\"frame_errors\":%lu",
          d->name, bus_type_str(d->type), bus_status_str(run, stale, now_ms),
          (unsigned long)run->frames_ok, (unsigned long)run->frame_errors);
    if (run->ever_ok)
    {
        int32_t idle = (int32_t)(now_ms - run->last_ok_ms);
        w_out(w, ",\"idle_ms\":%lu", (unsigned long)(idle < 0 ? 0 : idle));
    }
    if (run->frame_errors > 0)
    {
        int32_t idle = (int32_t)(now_ms - run->last_error_ms);
        w_out(w, ",\"error_idle_ms\":%lu", (unsigned long)(idle < 0 ? 0 : idle));
    }
    if (d->type == BUS_UART && run->parity_errors > 0)
    {
        w_out(w, ",\"parity_errors\":%lu", (unsigned long)run->parity_errors);
    }
    if (d->type == BUS_I2C)
    {
        w_out(w, ",\"nacks\":%lu", (unsigned long)run->nacks);
        w_out(w, ",\"addresses\":[");
        for (uint8_t k = 0; k < run->n_addrs; k++)
        {
            w_out(w, "%s{\"addr\":%u,\"reads\":%lu,\"writes\":%lu}",
                  k ? "," : "", run->addrs[k].addr,
                  (unsigned long)run->addrs[k].reads,
                  (unsigned long)run->addrs[k].writes);
        }
        w_out(w, "]");
    }
    if (d->detail)
    {
        w_out(w, ",\"detail\":\"%s\"", d->detail);
    }
    w_out(w, "}");
}

size_t gpio_state_format(char *buf, size_t len,
                         const pin_desc_t *pdescs, const pin_sample_t *samples,
                         size_t npins, const bus_desc_t *bdescs,
                         const bus_run_t *runs, size_t nbuses, uint32_t now_ms)
{
    writer_t w = {buf, len, 0};
    w_out(&w, "{\"type\":\"gpio_state\",\"pins\":[");
    bool first = true;
    for (size_t i = 0; i < npins; i++)
    {
        if (pdescs[i].gpio < 0 || !pdescs[i].name)
        {
            continue;
        }
        w_out(&w, "%s", first ? "" : ",");
        emit_pin(&w, &pdescs[i], &samples[i]);
        first = false;
    }
    w_out(&w, "]");
    if (bdescs && nbuses > 0)
    {
        w_out(&w, ",\"buses\":[");
        first = true;
        for (size_t i = 0; i < nbuses; i++)
        {
            if (!bdescs[i].name)
            {
                continue;
            }
            w_out(&w, "%s", first ? "" : ",");
            emit_bus(&w, &bdescs[i], &runs[i], now_ms);
            first = false;
        }
        w_out(&w, "]");
    }
    w_out(&w, "}");
    return w.off;
}
