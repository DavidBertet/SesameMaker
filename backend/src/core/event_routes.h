// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Tiny id-keyed event dispatcher (first match wins). Extracted from wifi.c
// so the dispatch semantics stay host-testable: ESP event bases are
// externs, not constant expressions, so wifi.c keeps one table per base
// and routes through here. No ESP-IDF includes.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*event_route_fn)(void *event_data);

typedef struct
{
    int32_t id;
    event_route_fn fn;
} event_route_t;

// Call the first entry whose id matches. Returns true when handled,
// false when no entry matches (event_data untouched).
static inline bool event_routes_dispatch(const event_route_t *routes, size_t n,
                                         int32_t id, void *event_data)
{
    for (size_t i = 0; i < n; i++)
    {
        if (routes[i].id == id)
        {
            routes[i].fn(event_data);
            return true;
        }
    }
    return false;
}
