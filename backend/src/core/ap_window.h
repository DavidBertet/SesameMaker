// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Pure decision helper for the fallback-AP auto-off window (AP_AUTO_OFF_S in
// constants.h). Kept free of ESP-IDF includes so host unit tests can cover the
// 0/-1/N sign convention wifi.c implements.

#pragma once

typedef enum
{
    AP_WINDOW_DISABLED, // 0: AP never starts (STA-only posture)
    AP_WINDOW_FOREVER,  // <0: AP stays on forever (insecure, debug only)
    AP_WINDOW_TIMED,    // >0: AP shuts off N seconds after it comes up
} ap_window_t;

static inline ap_window_t ap_window_action(int timeout_s)
{
    if (timeout_s == 0)
    {
        return AP_WINDOW_DISABLED;
    }
    if (timeout_s < 0)
    {
        return AP_WINDOW_FOREVER;
    }
    return AP_WINDOW_TIMED;
}
