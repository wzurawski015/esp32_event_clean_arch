#pragma once
#include <stdbool.h>
#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    char pattern_char; // Np. '\n'
} uart_svc_cfg_t;

// Startuje asynchroniczny serwis UART (task RX + subskrypcja TX)
bool services_uart_start(const ev_bus_t* bus, const uart_svc_cfg_t* cfg);

void services_uart_stop(void);

#ifdef __cplusplus
}
#endif
