// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Dry-contact pure logic, host testable. See drycontact.h.

#include "drycontact.h"

int drycontact_infer_both(bool open_hit, bool close_hit, bool last_open,
                          bool last_close)
{
    if (open_hit && close_hit)
    {
        return DRY_DOOR_UNKNOWN; // contradictory wiring/fault: don't guess
    }
    if (open_hit)
    {
        return DRY_DOOR_OPEN;
    }
    if (close_hit)
    {
        return DRY_DOOR_CLOSED;
    }
    // In between: the limit we just left tells the direction.
    if (last_close && !last_open)
    {
        return DRY_DOOR_OPENING;
    }
    if (last_open && !last_close)
    {
        return DRY_DOOR_CLOSING;
    }
    return DRY_DOOR_UNKNOWN;
}

int drycontact_infer_close_only(bool close_hit)
{
    return close_hit ? DRY_DOOR_CLOSED : DRY_DOOR_UNKNOWN;
}

bool drycontact_pulse_needed(int current, int target, bool *done)
{
    if (done)
    {
        *done = false;
    }
    if (target == DRY_TARGET_TOGGLE)
    {
        if (done)
        {
            *done = true;
        }
        return true;
    }
    switch (target)
    {
    case DRY_DOOR_OPEN:
        if (current == DRY_DOOR_OPEN)
        {
            if (done)
            {
                *done = true;
            }
            return false;
        }
        if (current == DRY_DOOR_OPENING)
        {
            return false; // already on its way
        }
        return true;
    case DRY_DOOR_CLOSED:
        if (current == DRY_DOOR_CLOSED)
        {
            if (done)
            {
                *done = true;
            }
            return false;
        }
        if (current == DRY_DOOR_CLOSING)
        {
            return false;
        }
        return true;
    case DRY_DOOR_STOPPED:
    default:
        // A blind relay press mid-travel stops the door on single-button
        // openers, but without sensors we cannot confirm motion at all.
        if (current == DRY_DOOR_OPENING || current == DRY_DOOR_CLOSING)
        {
            if (done)
            {
                *done = true;
            }
            return true;
        }
        if (done)
        {
            *done = true;
        }
        return false;
    }
}

bool drycontact_gpio_allowed(int8_t gpio)
{
    if (gpio < 0)
    {
        return false;
    }
    if (gpio >= 6 && gpio <= 11)
    {
        return false; // SPI flash bus
    }
    if (gpio >= 34 && gpio <= 39)
    {
        return false; // input-only, no pullup
    }
    return true;
}

bool drycontact_debounce(bool raw, uint32_t now_ms, bool *stable,
                         bool *last_raw, uint32_t *last_change_ms,
                         uint32_t debounce_ms)
{
    if (!stable || !last_raw || !last_change_ms)
    {
        return false;
    }
    if (raw != *last_raw)
    {
        *last_raw = raw;
        *last_change_ms = now_ms;
        return false;
    }
    if (*stable != raw && now_ms - *last_change_ms >= debounce_ms)
    {
        *stable = raw;
        return true;
    }
    return false;
}
