// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "raw_json.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct
{
    char *buf;
    size_t size;
    size_t off;
} writer_t;

// vfsnprintf that tracks the logical offset even when the buffer is too
// small, and that never writes past buf[size-1] (or writes at all when
// buf is NULL, for sizing).
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

static void emit_events(writer_t *w, const char *key,
                        const raw_json_event_t *log, size_t log_len,
                        uint32_t head)
{
    w_out(w, "\"%s\":[", key);
    bool first = true;
    for (size_t i = 0; i < log_len; i++)
    {
        const raw_json_event_t *entry = &log[(head + i) % log_len];
        if (entry->timestamp_ms == 0)
        {
            continue;
        }
        w_out(w, "%s{\"t\":%lu,\"byte\":%u}", first ? "" : ",",
              (unsigned long)entry->timestamp_ms, entry->byte);
        first = false;
    }
    w_out(w, "]");
}

size_t raw_json_garage_payload(char *buf, size_t size,
                               const raw_json_event_t *rx, size_t rx_len,
                               uint32_t rx_head, uint32_t rx_total,
                               const raw_json_event_t *tx, size_t tx_len,
                               uint32_t tx_head, uint32_t tx_total)
{
    writer_t w = {buf, size, 0};
    w_out(&w, "{\"type\":\"garage_raw\",");
    emit_events(&w, "rx", rx, rx_len, rx_head);
    w_out(&w, ",");
    emit_events(&w, "tx", tx, tx_len, tx_head);
    w_out(&w, ",\"rx_total\":%lu,\"tx_total\":%lu}",
          (unsigned long)rx_total, (unsigned long)tx_total);
    return w.off;
}