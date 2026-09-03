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

// Initialize the 1200 baud 8E1 half-duplex UART on the wall bus.
esp_err_t garage_uart_init(void);

// Read next available byte. Returns true when *byte is filled.
bool garage_uart_read(uint8_t *byte);

// Send one byte on the bus (handles echo suppression).
esp_err_t garage_uart_send(uint8_t byte);

// Milliseconds since boot, for timestamping.
uint32_t garage_uart_now_ms(void);

// Rolling logs of raw bus traffic for the protocol inspector.
void garage_uart_log_rx(uint8_t byte);
void garage_uart_log_tx(uint8_t byte);
const garage_uart_event_t *garage_uart_rx_log(void);
const garage_uart_event_t *garage_uart_tx_log(void);
uint32_t garage_uart_rx_byte_count(void);
uint32_t garage_uart_tx_byte_count(void);
