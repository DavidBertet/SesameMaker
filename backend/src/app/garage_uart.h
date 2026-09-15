// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define GARAGE_UART_RX_LOG_SIZE 32
#define GARAGE_UART_TX_LOG_SIZE 16

typedef struct
{
    uint32_t timestamp_ms;
    uint8_t byte;
} garage_uart_event_t;

// Initialize the wall-bus UART. Starts in secplus1 mode (1200 baud 8E1);
// switch modes when the active protocol changes.
esp_err_t garage_uart_init(void);

// Half-duplex bus modes. Secplus1: 1200 baud 8E1 single bytes. Secplus2:
// 9600 baud 8N1 19-byte packets with a break pulse ahead of each TX.
esp_err_t garage_uart_set_secplus1_mode(void);
esp_err_t garage_uart_set_secplus2_mode(void);

// Read next available byte. Returns true when *byte is filled.
bool garage_uart_read(uint8_t *byte);

// Send one byte on the bus (handles echo suppression).
esp_err_t garage_uart_send(uint8_t byte);

// Send a secplus2 packet: break pulse (bus LOW then HIGH) + bytes, then
// drain the 19 echo bytes of our own transmission. UNTESTED ON HARDWARE:
// break polarity/timing is per the ESP32 wall-device implementations and
// has not been validated against a real opener with this interface circuit.
esp_err_t garage_uart_send_packet(const uint8_t *data, size_t len);

// Milliseconds since boot, for timestamping.
uint32_t garage_uart_now_ms(void);

// Rolling logs of raw bus traffic for the protocol inspector.
void garage_uart_log_rx(uint8_t byte);
void garage_uart_log_tx(uint8_t byte);
const garage_uart_event_t *garage_uart_rx_log(void);
const garage_uart_event_t *garage_uart_tx_log(void);
uint32_t garage_uart_rx_byte_count(void);
uint32_t garage_uart_tx_byte_count(void);
