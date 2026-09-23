// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit service selection from protocol caps. Pure logic, no ESP-IDF or
// HAP includes so it is unit tested on the host like zigbee_press.h.
//
// The accessory always has the door service; light/lock/motion follow the
// active protocol (secplus: all; dry-contact: none). The report path
// (push_u8/push_bool) is NULL-safe, so skipped services simply never push.

#pragma once

#include "protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    bool light;
    bool lock;
    bool motion;
  } homekit_services_t;

  static inline homekit_services_t homekit_services_for_caps(protocol_caps_t caps)
  {
    homekit_services_t s;
    s.light = caps.light;
    s.lock = caps.lock;
    s.motion = caps.motion;
    return s;
  }

#ifdef __cplusplus
}
#endif
