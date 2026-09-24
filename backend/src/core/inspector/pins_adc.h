// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Lazy ADC oneshot reads for PIN_TYPE_ANALOG pins: per-unit init, channels
// configured on first use, one curve-fitting calibration handle per
// (unit, channel) slot (recreated if a channel asks for a different
// attenuation than the handle was built with).

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Read one ADC-capable GPIO (raw + calibrated mV). False when the GPIO is
// not ADC-capable or calibration/read fails. atten_cfg is 0..3
// (ADC_ATTEN_DB_0..DB_12), anything else means the default DB_12.
bool pins_adc_read(int8_t gpio, int atten_cfg, int *raw_out, int *mv_out);
