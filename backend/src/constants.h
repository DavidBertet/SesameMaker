#pragma once

#define OTA_PASSWORD "your_secure_password_here"

// ==== SesameMaker: GPIO pins ====
// secplus1 wall bus (1200 baud 8E1 half duplex) via interface circuit:
// TX: open-collector driver (NPN/optocoupler) pulling the wall line low
// RX: voltage divider scaling the wall line down to <= 3.3 V
#define GARAGE_TX_GPIO 4
#define GARAGE_RX_GPIO 16
#define GARAGE_UART_NUM 1 // UART1 (UART0 is the USB console)

// ==== SesameMaker: task timing ====
#define GARAGE_TICK_MS 25
#define GARAGE_BROADCAST_INTERVAL_MS 1000
