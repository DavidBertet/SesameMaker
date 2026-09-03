// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// secplus1 bus transport: 1200 baud 8E1 UART, half duplex on the wall
// control line. TX drives the line through the interface circuit, RX
// listens. Our own transmissions echo back on RX and are discarded.

#include "garage_uart.h"

#include "constants.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "GARAGE_UART";

static garage_uart_event_t s_rx_log[GARAGE_UART_RX_LOG_SIZE];
static garage_uart_event_t s_tx_log[GARAGE_UART_TX_LOG_SIZE];
static uint8_t s_rx_log_head = 0;
static uint8_t s_tx_log_head = 0;
static uint32_t s_rx_count = 0;
static uint32_t s_tx_count = 0;

uint32_t garage_uart_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

esp_err_t garage_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 1200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(GARAGE_UART_NUM, &uart_config);
    if (ret != ESP_OK)
    {
        return ret;
    }
    // The bus driver is a common-emitter NPN (inverting) that pulls the wall
    // line low for a '0'. Invert TX so UART idle (low after inversion) keeps
    // the transistor off and the bus idling high, and '0' bits pull it low.
    // Applied BEFORE uart_set_pin so the default UART idle (high) never
    // drives the base on at the moment the pin is routed to the peripheral.
    ret = uart_set_line_inverse(GARAGE_UART_NUM, UART_SIGNAL_TXD_INV);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = uart_set_pin(GARAGE_UART_NUM, GARAGE_TX_GPIO, GARAGE_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK)
    {
        return ret;
    }
    // uart_set_pin enables the internal pull-up on the RX pin; that fights the
    // divider's bottom leg and floats the node high (RX stuck at 1). Run the
    // line fully flaccid so the wall divider alone sets the RX level.
    gpio_set_pull_mode(GARAGE_RX_GPIO, GPIO_FLOATING);
    ret = uart_driver_install(GARAGE_UART_NUM, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ESP_LOGI(TAG, "secplus1 UART ready: 1200 8E1 tx=%d rx=%d",
             GARAGE_TX_GPIO, GARAGE_RX_GPIO);
    return ESP_OK;
}

bool garage_uart_read(uint8_t *byte)
{
    if (!byte)
    {
        return false;
    }
    return uart_read_bytes(GARAGE_UART_NUM, byte, 1, 0) == 1;
}

esp_err_t garage_uart_send(uint8_t byte)
{
    // One byte at 1200 baud 8E1 (11 bits) takes ~9.2 ms. The wall line is a
    // half-duplex bus; if our RX pin hears our own frame it arrives first
    // (by arrival order), so consume that single echo byte synchronously and
    // discard it regardless of its decoded value. On boards where the RX
    // divider does not pick up our TX this deadline loop simply times out
    // and changes nothing. Opener responses are never touched.
    esp_err_t ret = uart_write_bytes(GARAGE_UART_NUM, &byte, 1) >= 0
                        ? ESP_OK
                        : ESP_FAIL;
    uart_wait_tx_done(GARAGE_UART_NUM, pdMS_TO_TICKS(50));
    size_t avail = 0;
    int64_t deadline = esp_timer_get_time() + 25000; // own echo, if any
    while (esp_timer_get_time() < deadline)
    {
        uart_get_buffered_data_len(GARAGE_UART_NUM, &avail);
        if (avail > 0)
        {
            break;
        }
        vTaskDelay(1);
    }
    if (avail > 0)
    {
        uint8_t echo;
        uart_read_bytes(GARAGE_UART_NUM, &echo, 1, 0);
    }
    garage_uart_log_tx(byte);
    return ret;
}

void garage_uart_log_rx(uint8_t byte)
{
    s_rx_log[s_rx_log_head].timestamp_ms = garage_uart_now_ms();
    s_rx_log[s_rx_log_head].byte = byte;
    s_rx_log_head = (s_rx_log_head + 1) % GARAGE_UART_RX_LOG_SIZE;
    s_rx_count++;
}

void garage_uart_log_tx(uint8_t byte)
{
    s_tx_log[s_tx_log_head].timestamp_ms = garage_uart_now_ms();
    s_tx_log[s_tx_log_head].byte = byte;
    s_tx_log_head = (s_tx_log_head + 1) % GARAGE_UART_TX_LOG_SIZE;
    s_tx_count++;
}

const garage_uart_event_t *garage_uart_rx_log(void)
{
    return s_rx_log;
}

const garage_uart_event_t *garage_uart_tx_log(void)
{
    return s_tx_log;
}

uint32_t garage_uart_rx_byte_count(void)
{
    return s_rx_count;
}

uint32_t garage_uart_tx_byte_count(void)
{
    return s_tx_count;
}
