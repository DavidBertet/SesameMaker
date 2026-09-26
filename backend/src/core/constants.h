#pragma once

// OTA upload password default. Empty = open (no password required); set a
// real password over USB provisioning (Improv 0x10) or --upload-password,
// which stores it in NVS and takes precedence over this default.
#define OTA_PASSWORD ""

// ==== SesameMaker: GPIO pins ====
// secplus1 wall bus (1200 baud 8E1 half duplex) via interface circuit:
// TX: open-collector driver (NPN/optocoupler) pulling the wall line low
// RX: voltage divider scaling the wall line down to <= 3.3 V
#define GARAGE_TX_GPIO 4
#define GARAGE_RX_GPIO 16
#define GARAGE_UART_NUM 1 // UART1 (UART0 is the USB console)

// ==== SesameMaker: dry-contact GPIOs (defaults, overridable via WS config) ====
// Relay: NO contacts across the opener wall-button terminals.
// Reeds: magnetic limit switches, closed level = door at that end.
// WARNING (classic ESP32): GPIO6-11 are the SPI flash bus and GPIO34-39 are
// input-only with no pullup - both ranges are rejected by cfg_valid().
// Defaults below avoid them (relay=5, reeds=17/18 are free on the devkit).
#define DRY_RELAY_GPIO 5
#define DRY_OPEN_LIMIT_GPIO 17
#define DRY_CLOSE_LIMIT_GPIO 18
#define DRY_PULSE_MS_DEFAULT 500
#define DRY_DEBOUNCE_MS_DEFAULT 200
#define DRY_TRAVEL_S_DEFAULT 15

// ==== SesameMaker: Zigbee (802.15.4 targets, see CONFIG_SOC_IEEE802154_SUPPORTED) ====
// BOOT strapping pin doubles as the pairing button at runtime (sampled
// after boot only; never held at reset or the chip enters download mode).
// Value -1 = no button / no Zigbee on this target.
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
#define ZIGBEE_BOOT_GPIO 9
#else
#define ZIGBEE_BOOT_GPIO -1
#endif

// ==== SesameMaker: fallback AP auto-off ====
// Anyone in radio range can join the "SesameMaker" config AP and drive the
// door, so it must not stay up forever.
//   0  = AP never starts (STA-only; configure via serial/OTA)
//   -1 = AP stays on forever (insecure, debug only)
//   >0 = AP shuts off N seconds after it comes up (missed it? reboot).
#define AP_AUTO_OFF_S 120

// ==== SesameMaker: task timing ====
#define GARAGE_TICK_MS 25
#define GARAGE_BROADCAST_INTERVAL_MS 1000
