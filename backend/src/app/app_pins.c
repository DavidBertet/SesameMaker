// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "app_pins.h"

#include "constants.h"
#include "drycontact.h"
#include "pin_inspector.h"
#include "protocol_drycontact.h"

#include <stddef.h>

#define PUSH(out, max, n, ...)                       \
    do                                               \
    {                                                \
        if ((n) < (max))                             \
            (out)[(n)++] = (pin_desc_t){__VA_ARGS__}; \
    } while (0)

static size_t sesame_describe(pin_desc_t *out, size_t max)
{
    size_t n = 0;

    // secplus1 wall bus (1200 baud 8E1 half duplex): two lines of one bus.
    PUSH(out, max, n,
         .gpio = GARAGE_TX_GPIO, .name = "GARAGE_TX",
         .role = "secplus1 TX driver", .type = PIN_TYPE_UART,
         .is_output = true,
         .detail = "open-collector - 1200 baud 8E1 half-duplex");
    PUSH(out, max, n,
         .gpio = GARAGE_RX_GPIO, .name = "GARAGE_RX",
         .role = "secplus1 RX line", .type = PIN_TYPE_UART,
         .is_output = false,
         .detail = "voltage divider - 1200 baud 8E1");

    // Dry-contact relay + reeds follow the runtime config (not the
    // constants.h defaults) so a remapped pin shows up correctly.
    dry_cfg_t cfg;
    if (protocol_drycontact_get_cfg(&cfg) == ESP_OK)
    {
        PUSH(out, max, n,
             .gpio = cfg.relay_gpio, .name = "DRY_RELAY",
             .role = "dry relay driver", .type = PIN_TYPE_DIGITAL,
             .is_output = true);
        if (cfg.sensor_mode == DRY_SENSORS_BOTH)
        {
            PUSH(out, max, n,
                 .gpio = cfg.open_gpio, .name = "DRY_OPEN",
                 .role = "dry open reed", .type = PIN_TYPE_DIGITAL,
                 .is_output = false, .active_low = cfg.active_low,
                 .report_hit = true);
            PUSH(out, max, n,
                 .gpio = cfg.close_gpio, .name = "DRY_CLOSE",
                 .role = "dry close reed", .type = PIN_TYPE_DIGITAL,
                 .is_output = false, .active_low = cfg.active_low,
                 .report_hit = true);
        }
        else if (cfg.sensor_mode == DRY_SENSORS_CLOSE_ONLY)
        {
            PUSH(out, max, n,
                 .gpio = cfg.close_gpio, .name = "DRY_CLOSE",
                 .role = "dry close reed", .type = PIN_TYPE_DIGITAL,
                 .is_output = false, .active_low = cfg.active_low,
                 .report_hit = true);
        }
    }

#if ZIGBEE_BOOT_GPIO >= 0
    PUSH(out, max, n,
         .gpio = ZIGBEE_BOOT_GPIO, .name = "ZIGBEE_BOOT",
         .role = "pairing button", .type = PIN_TYPE_DIGITAL,
         .is_output = false, .detail = "hold 3s pair - 10s+ reset");
#endif

    return n;
}

// Wall-bus framing validation: the RX line carries opener + panel traffic,
// so its pattern proves the bus is alive and well-formed. TX is our own
// driver output; nothing to validate there.
static size_t sesame_buses(bus_desc_t *out, size_t max)
{
    size_t n = 0;
    if (n < max)
    {
        out[n++] = (bus_desc_t){
            .name = "WALLBUS",
            .type = BUS_UART,
            .rx_gpio = GARAGE_RX_GPIO,
            .baud = 1200,
            .data_bits = 8,
            .parity = BUS_PARITY_EVEN,
            .stop_bits = 1,
            .detail = "1200 baud 8E1",
        };
    }
    return n;
}

void app_pins_register(void)
{
    pin_inspector_set_provider(sesame_describe);
    pin_inspector_set_bus_provider(sesame_buses);
}
