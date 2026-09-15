// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Protocol abstraction: every opener type (Security+ 1.0, dry-contact relay,
// Security+ 2.0) is identified by a protocol_id_t and described by a
// capability set. The UI adapts from caps alone and never branches on the id.
//
// This header is pure logic (no ESP-IDF includes) so it is unit tested on the
// host alongside secplus1.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    PROTOCOL_SECPLUS1 = 0,   // 1200 baud 8E1 wall bus, full bus feedback
    PROTOCOL_DRYCONTACT = 1, // relay pulse + 0/1/2 reed limit sensors
    PROTOCOL_SECPLUS2 = 2,   // stub: not implemented yet
} protocol_id_t;

#define PROTOCOL_COUNT 3

// Capability set: what the active protocol can report or drive.
// secplus1/secplus2: bus feedback baked in (light/lock/obstruction/motion/panel).
// dry-contact: none of those; door position comes from reed sensors instead.
typedef struct
{
    bool light;
    bool obstruction;
    bool motion;
    bool lock;
    bool panel;
    bool sensors; // reed limit inputs (dry-contact only)
} protocol_caps_t;

static inline const char *protocol_id_str(protocol_id_t id)
{
    switch (id)
    {
    case PROTOCOL_SECPLUS1:
        return "secplus1";
    case PROTOCOL_DRYCONTACT:
        return "drycontact";
    case PROTOCOL_SECPLUS2:
        return "secplus2";
    default:
        return "unknown";
    }
}

static inline bool protocol_id_from_str(const char *s, protocol_id_t *out)
{
    if (!s || !out)
    {
        return false;
    }
    for (int i = 0; i < PROTOCOL_COUNT; i++)
    {
        if (strcmp(s, protocol_id_str((protocol_id_t)i)) == 0)
        {
            *out = (protocol_id_t)i;
            return true;
        }
    }
    return false;
}

static inline bool protocol_id_valid(int v)
{
    return v >= 0 && v < PROTOCOL_COUNT;
}

// True once a driver exists behind the id.
static inline bool protocol_id_supported(protocol_id_t id)
{
    return id == PROTOCOL_SECPLUS1 || id == PROTOCOL_DRYCONTACT ||
           id == PROTOCOL_SECPLUS2;
}

static inline protocol_caps_t protocol_caps_for(protocol_id_t id)
{
    protocol_caps_t c;
    memset(&c, 0, sizeof(c));
    switch (id)
    {
    case PROTOCOL_SECPLUS1:
        c.light = true;
        c.obstruction = true;
        c.motion = true;
        c.lock = true;
        c.panel = true;
        c.sensors = false;
        break;
    case PROTOCOL_SECPLUS2:
        // Same bus feedback as secplus1, except wall-panel detection: on a
        // secplus2 bus we ARE a wall device, there is nothing to detect.
        c.light = true;
        c.obstruction = true;
        c.motion = true;
        c.lock = true;
        c.panel = false;
        c.sensors = false;
        break;
    case PROTOCOL_DRYCONTACT:
        // No bus feedback at all; position only via reed sensors.
        c.sensors = true;
        break;
    default:
        break;
    }
    return c;
}

// JSON fragment: {"light":true,"lock":false,...} (no surrounding key).
static inline size_t protocol_caps_json(const protocol_caps_t *c, char *buf,
                                        size_t len)
{
    if (!c)
    {
        return 0;
    }
    return (size_t)snprintf(buf, len,
                            "\"light\":%s,\"lock\":%s,\"obstruction\":%s,"
                            "\"motion\":%s,\"panel\":%s,\"sensors\":%s",
                            c->light ? "true" : "false",
                            c->lock ? "true" : "false",
                            c->obstruction ? "true" : "false",
                            c->motion ? "true" : "false",
                            c->panel ? "true" : "false",
                            c->sensors ? "true" : "false");
}

#ifdef __cplusplus
}
#endif
