// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "pins_adc.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include <stddef.h>

#define ADC_MAX_CH 10

typedef struct
{
    adc_cali_handle_t handle;
    int atten;
} adc_cali_slot_t;

static adc_oneshot_unit_handle_t s_adc_unit[2];
static adc_cali_slot_t s_adc_cali[2][ADC_MAX_CH];
static uint32_t s_adc_ch_mask[2];

bool pins_adc_read(int8_t gpio, int atten_cfg, int *raw_out, int *mv_out)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel((int)gpio, &unit, &channel) != ESP_OK)
    {
        return false;
    }
    if (unit < 0 || unit > 1)
    {
        return false;
    }
    adc_atten_t atten = (atten_cfg >= ADC_ATTEN_DB_0 && atten_cfg <= ADC_ATTEN_DB_12)
                            ? (adc_atten_t)atten_cfg
                            : ADC_ATTEN_DB_12;
    if (!s_adc_unit[unit])
    {
        adc_oneshot_unit_init_cfg_t ucfg = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        if (adc_oneshot_new_unit(&ucfg, &s_adc_unit[unit]) != ESP_OK)
        {
            return false;
        }
    }
    if (!(s_adc_ch_mask[unit] & (1u << (unsigned)channel)))
    {
        adc_oneshot_chan_cfg_t ccfg = {
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_oneshot_config_channel(s_adc_unit[unit], channel, &ccfg) != ESP_OK)
        {
            return false;
        }
        s_adc_ch_mask[unit] |= (1u << (unsigned)channel);
    }
    if ((unsigned)channel >= ADC_MAX_CH)
    {
        return false;
    }
    adc_cali_slot_t *slot = &s_adc_cali[unit][channel];
    if (!slot->handle || slot->atten != (int)atten)
    {
        if (slot->handle)
        {
            adc_cali_delete_scheme_curve_fitting(slot->handle);
            slot->handle = NULL;
        }
        adc_cali_curve_fitting_config_t lcfg = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&lcfg, &slot->handle) != ESP_OK)
        {
            slot->handle = NULL;
            return false;
        }
        slot->atten = (int)atten;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc_unit[unit], channel, &raw) != ESP_OK)
    {
        return false;
    }
    int mv = 0;
    if (adc_cali_raw_to_voltage(slot->handle, raw, &mv) != ESP_OK)
    {
        return false;
    }
    *raw_out = raw;
    *mv_out = mv;
    return true;
}
