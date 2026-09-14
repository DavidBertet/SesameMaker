#pragma once

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage product feature flags for the generic settings endpoint (app).
// Registers the static "features" fragment; the air-quality sensor
// provides its own (e.g. its sensors) and reuses the endpoint untouched.

#ifdef __cplusplus
extern "C"
{
#endif

  // Register garage feature flags. Call once before settings are served.
  void settings_garage_register(void);

#ifdef __cplusplus
}
#endif
