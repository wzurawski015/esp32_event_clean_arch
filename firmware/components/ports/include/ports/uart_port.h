#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ports/errors.h"

// Dołączamy FreeRTOS, aby udostępnić QueueHandle_t do QueueSet
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uart_port* uart_port_handle_t;

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    int rx_buf_size;
    int tx_buf_size;
} uart_cfg_t;

// Tworzenie/Usuwanie
port_err_t uart_port_create(const uart_cfg_t* cfg, uart_port_handle_t* out);
port_err_t uart_port_delete(uart_port_handle_t p);

// Konfiguracja
port_err_t uart_port_enable_pattern_det(uart_port_handle_t p, char c);

// IO
int uart_port_write(uart_port_handle_t p, const void* data, size_t len);
int uart_port_read(uart_port_handle_t p, void* buf, size_t max_len, uint32_t timeout_ms);

// Low-level Event Access (dla QueueSet w Service)
QueueHandle_t uart_port_get_event_queue(uart_port_handle_t p);
size_t uart_port_get_buffered_len(uart_port_handle_t p);
int uart_port_pop_pattern(uart_port_handle_t p);

// Helpers do mapowania typów zdarzeń (unikanie wycieku "driver/uart.h" do service)
bool uart_port_is_pattern_event(int evt_type);
bool uart_port_is_data_event(int evt_type);

#ifdef __cplusplus
}
#endif
