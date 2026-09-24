// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Shared sizing JSON writer: vsnprintf that tracks the logical offset even
// when the buffer is too small, and never writes past buf[size-1] (or
// writes at all when buf is NULL, for sizing). Header-only so both core/
// and app/ payload builders share it with no extra TU in the host tests.
// No ESP-IDF dependencies, host-testable.

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
    char *buf;
    size_t size;
    size_t off;
} json_writer_t;

static inline void json_w_out(json_writer_t *w, const char *fmt, ...)
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
