#include "ports/uart_port.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "UART_PORT";

struct uart_port {
    uart_port_t uart_num;
    QueueHandle_t evt_queue;
};

port_err_t uart_port_create(const uart_cfg_t* cfg, uart_port_handle_t* out)
{
    if (!cfg || !out) return PORT_ERR_INVALID_ARG;

    struct uart_port* p = calloc(1, sizeof(struct uart_port));
    if (!p) return PORT_FAIL;

    p->uart_num = (uart_port_t)cfg->uart_num;

    uart_config_t ucfg = {
        .baud_rate = cfg->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_param_config(p->uart_num, &ucfg) != ESP_OK) {
        free(p);
        return PORT_FAIL;
    }

    if (uart_set_pin(p->uart_num, cfg->tx_pin, cfg->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        free(p);
        return PORT_FAIL;
    }

    // Instalacja sterownika z kolejką zdarzeń
    if (uart_driver_install(p->uart_num, cfg->rx_buf_size, cfg->tx_buf_size, 20, &p->evt_queue, 0) != ESP_OK) {
        free(p);
        return PORT_FAIL;
    }

    *out = p;
    ESP_LOGI(TAG, "UART%d initialized: TX=%d, RX=%d, Baud=%d", p->uart_num, cfg->tx_pin, cfg->rx_pin, cfg->baud_rate);
    return PORT_OK;
}

port_err_t uart_port_delete(uart_port_handle_t p)
{
    if (!p) return PORT_OK;
    uart_driver_delete(p->uart_num);
    free(p);
    return PORT_OK;
}

port_err_t uart_port_enable_pattern_det(uart_port_handle_t p, char c)
{
    if (!p) return PORT_ERR_INVALID_ARG;
    // Wykrywanie znaku 'c'
    if (uart_enable_pattern_det_baud_intr(p->uart_num, c, 1, 9, 0, 0) != ESP_OK) {
        return PORT_FAIL;
    }
    uart_pattern_queue_reset(p->uart_num, 20);
    return PORT_OK;
}

int uart_port_write(uart_port_handle_t p, const void* data, size_t len)
{
    if (!p) return -1;
    return uart_write_bytes(p->uart_num, data, len);
}

int uart_port_read(uart_port_handle_t p, void* buf, size_t max_len, uint32_t timeout_ms)
{
    if (!p) return -1;
    return uart_read_bytes(p->uart_num, buf, max_len, timeout_ms / portTICK_PERIOD_MS);
}

QueueHandle_t uart_port_get_event_queue(uart_port_handle_t p)
{
    return p ? p->evt_queue : NULL;
}

bool uart_port_is_pattern_event(int evt_type) {
    return (evt_type == UART_PATTERN_DET);
}

bool uart_port_is_data_event(int evt_type) {
    return (evt_type == UART_DATA);
}

size_t uart_port_get_buffered_len(uart_port_handle_t p) {
    if (!p) return 0;
    size_t len = 0;
    uart_get_buffered_data_len(p->uart_num, &len);
    return len;
}

int uart_port_pop_pattern(uart_port_handle_t p) {
    return (p) ? uart_pattern_pop_pos(p->uart_num) : -1;
}
