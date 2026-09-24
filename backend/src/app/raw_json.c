// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "raw_json.h"

#include "json_writer.h"

static void emit_events(json_writer_t *w, const char *key,
                        const raw_json_event_t *log, size_t log_len,
                        uint32_t head)
{
    json_w_out(w, "\"%s\":[", key);
    bool first = true;
    for (size_t i = 0; i < log_len; i++)
    {
        const raw_json_event_t *entry = &log[(head + i) % log_len];
        if (entry->timestamp_ms == 0)
        {
            continue;
        }
        json_w_out(w, "%s{\"t\":%lu,\"byte\":%u}", first ? "" : ",",
              (unsigned long)entry->timestamp_ms, entry->byte);
        first = false;
    }
    json_w_out(w, "]");
}

size_t raw_json_garage_payload(char *buf, size_t size,
                               const raw_json_event_t *rx, size_t rx_len,
                               uint32_t rx_head, uint32_t rx_total,
                               const raw_json_event_t *tx, size_t tx_len,
                               uint32_t tx_head, uint32_t tx_total)
{
    json_writer_t w = {buf, size, 0};
    json_w_out(&w, "{\"type\":\"garage_raw\",");
    emit_events(&w, "rx", rx, rx_len, rx_head);
    json_w_out(&w, ",");
    emit_events(&w, "tx", tx, tx_len, tx_head);
    json_w_out(&w, ",\"rx_total\":%lu,\"tx_total\":%lu}",
          (unsigned long)rx_total, (unsigned long)tx_total);
    return w.off;
}