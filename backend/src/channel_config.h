// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Zigbee scan-channel configuration. Pure logic, no ESP-IDF includes so it
// is unit tested on the host like zigbee_press.h.
//
// channel_cfg semantics (0 = auto):
//   - 0        -> scan all 16 channels (11..26), low scan duration (fast on
//                 a healthy link; the wider mask is the tradeoff)
//   - 11..26   -> pin BDB scanning to that single channel, higher scan
//                 duration (dwells longer per channel to catch a marginal
//                 beacon-response window). Choses the value that breaks the
//                 "must hold the device next to the router" failure mode.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// BDB channel mask for all channels the ESP32-C6 supports (11..26).
#define ZB_CHANNEL_MASK_ALL 0x07FFF800u
// BDB channel mask for a single channel N (bit N).
#define ZB_CHANNEL_MASK_SINGLE(ch) (1u << (ch))
// Lower energy-scan duration used while sweeping all channels.
#define ZB_SCAN_DURATION_AUTO 4
// Dwell used when pinned to one channel (longer to catch weak beacons).
#define ZB_SCAN_DURATION_FIXED 6

// Valid channels: 0 (auto) or 11..26.
#define ZB_CHANNEL_CFG_MIN 11
#define ZB_CHANNEL_CFG_MAX 26

static inline bool zigbee_channel_cfg_valid(int channel)
{
    return channel == 0 || (channel >= ZB_CHANNEL_CFG_MIN && channel <= ZB_CHANNEL_CFG_MAX);
}

// BDB primary+secondary channel mask for the given configured channel.
static inline uint32_t zigbee_channel_mask(int channel)
{
    if (channel >= ZB_CHANNEL_CFG_MIN && channel <= ZB_CHANNEL_CFG_MAX)
        return ZB_CHANNEL_MASK_SINGLE(channel);
    return ZB_CHANNEL_MASK_ALL;
}

// Energy-scan duration for the given configured channel.
static inline uint8_t zigbee_channel_scan_duration(int channel)
{
    if (channel >= ZB_CHANNEL_CFG_MIN && channel <= ZB_CHANNEL_CFG_MAX)
        return ZB_SCAN_DURATION_FIXED;
    return ZB_SCAN_DURATION_AUTO;
}

#ifdef __cplusplus
}
#endif