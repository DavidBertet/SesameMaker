// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Garage product feature flags for the generic settings endpoint (app).

#include "settings_garage.h"

#include "ws_settings.h"

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
