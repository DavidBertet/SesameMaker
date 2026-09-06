#pragma once

#define OTA_PASSWORD "your_secure_password_here"

// Build-time WiFi defaults (empty placeholders; real secrets live in
// gitignored secrets.h, written by `install.sh --wifi-ssid`).
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""
#define DEFAULT_WIFI_GEN ""

#if __has_include("secrets.h")
#include "secrets.h"
#endif

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

// ==== SesameMaker: task timing ====
#define GARAGE_TICK_MS 25
#define GARAGE_BROADCAST_INTERVAL_MS 1000
