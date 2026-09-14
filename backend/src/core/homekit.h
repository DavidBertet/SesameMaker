#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// HomeKit accessory transport (core): HAP lifecycle, per-device setup
// code provisioning, pairing-state query, and setup-payload (QR) data.
// Knows nothing about the product: the app builds its accessory
// (services + characteristics + write handlers) through homekit_device_t.
// Mirrors the zb_transport split: transport here, content in the app.

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    // Build the accessory: hap_acc_create + hap_serv_*_create + write
    // callbacks + hap_add_accessory. Called once before hap_start().
    void (*build_accessory)(void);
    // HAP category id for the setup payload (e.g. garage door opener).
    int category_id;
  } homekit_device_t;

  // Bind product content. Call once before homekit_start().
  void homekit_register_device(const homekit_device_t *dev);

  // Start the HAP task (idempotent): waits for Wi-Fi (bounded), provisions
  // the setup code, inits HAP, builds the accessory, starts HAP. Loud
  // failures: hap_start() errors are fatal-logged, never swallowed (the
  // stock example ignores the return and dies silent).
  void homekit_start(void);

  // True once HAP is up.
  bool homekit_started(void);
  // True when at least one controller is paired. False before start.
  bool homekit_is_paired(void);

  // Setup payload URI ("X-HM://...") for the QR code, or NULL when paired
  // (the code is only shown while pairable). Valid until the next call;
  // do not free.
  const char *homekit_setup_payload(void);

#ifdef __cplusplus
}
#endif
