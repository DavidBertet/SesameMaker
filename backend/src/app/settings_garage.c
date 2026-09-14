// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage product feature flags for the generic settings endpoint (app).

#include "settings_garage.h"

#include "ws_settings.h"

// NOTE: this TU includes no IDF headers, so CONFIG_ symbols would be
// invisible without this (other files get sdkconfig.h transitively via
// IDF includes — accidental, do not rely on it). The #ifdef below
// silently took the wrong branch before this include existed.
#include "sdkconfig.h"

void settings_garage_register(void)
{
    ws_settings_register_features(
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
        "\"zigbee\":true"
#else
        "\"zigbee\":false"
#endif
    );
}
