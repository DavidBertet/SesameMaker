// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

// SesameMaker pin table for the generic inspector: registers a provider that
// describes the secplus1 wall bus, the dry-contact relay/reeds (runtime NVS
// config, so the table is rebuilt on every sample) and the Zigbee button.

void app_pins_register(void);
