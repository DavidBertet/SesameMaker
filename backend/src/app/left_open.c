// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Left-open escalation, pure logic, host testable. See left_open.h.

#include "left_open.h"

#define LEFT_OPEN_BLOCKED_RETRY_MS 60000

void left_open_defaults(left_open_cfg_t *cfg)
{
    cfg->enabled = false;
    cfg->warn_s = 600;
    cfg->close_s = 0;
    cfg->webhook[0] = '\0';
}

bool left_open_valid(const left_open_cfg_t *cfg)
{
    if (cfg->warn_s > LEFT_OPEN_DURATION_MAX_S || cfg->close_s > LEFT_OPEN_DURATION_MAX_S)
    {
        return false;
    }
    size_t n = 0;
    while (n <= LEFT_OPEN_WEBHOOK_MAX && cfg->webhook[n] != '\0')
    {
        n++;
    }
    if (n > LEFT_OPEN_WEBHOOK_MAX)
    {
        return false;
    }
    // Warnings need a channel: browser tab notifications are best-effort and
    // must never be the warning path. Auto-close alone stays optional.
    if (cfg->enabled && cfg->warn_s > 0 && n == 0)
    {
        return false;
    }
    return true;
}

uint8_t left_open_step(left_open_state_t *st, uint32_t now_ms, bool door_open,
                       bool obstructed, const left_open_cfg_t *cfg)
{
    if (!cfg->enabled || !door_open)
    {
        st->open = false;
        st->open_since_ms = 0;
        st->warned = false;
        st->blocked = false;
        st->blocked_since_ms = 0;
        return LEFT_OPEN_NONE;
    }
    if (!st->open)
    {
        st->open = true;
        st->open_since_ms = now_ms;
        st->warned = false;
        st->blocked = false;
        st->blocked_since_ms = 0;
        return LEFT_OPEN_NONE;
    }
    uint8_t actions = LEFT_OPEN_NONE;
    uint32_t elapsed_ms = now_ms - st->open_since_ms;
    if (!st->warned && cfg->warn_s > 0 && elapsed_ms >= cfg->warn_s * 1000u)
    {
        st->warned = true;
        actions |= LEFT_OPEN_WARN;
    }
    if (cfg->close_s > 0 && elapsed_ms >= cfg->close_s * 1000u)
    {
        if (obstructed)
        {
            if (!st->blocked || now_ms - st->blocked_since_ms >= LEFT_OPEN_BLOCKED_RETRY_MS)
            {
                st->blocked = true;
                st->blocked_since_ms = now_ms;
                actions |= LEFT_OPEN_BLOCKED;
            }
        }
        else
        {
            st->blocked = false;
            st->blocked_since_ms = 0;
            st->open_since_ms = now_ms; // restart the window (see header)
            actions |= LEFT_OPEN_CLOSE;
            if (!st->warned)
            {
                st->warned = true;
                actions |= LEFT_OPEN_WARN;
            }
        }
    }
    return actions;
}
