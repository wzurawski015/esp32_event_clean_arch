#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "idf_i2c_port.h"   /* i2c_bus_t, i2c_dev_t */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_OP_TX = 0,
    I2C_OP_RX,
    I2C_OP_TXRX,
} i2c_op_t;

typedef struct {
    i2c_op_t   op;
    i2c_dev_t* dev;
    const uint8_t* tx;   /* może być NULL; kopiowane do wewnętrznego bufora */
    size_t     txlen;
    uint8_t*   rx;       /* może być NULL; dane są zapisywane po zakończeniu */
    size_t     rxlen;
    uint32_t   timeout_ms;
    void*      user;     /* przeźroczysty identyfikator */
} i2c_req_t;

/* start workera: długość kolejki, rozmiar stosu, priorytet taska */
bool services_i2c_start(int queue_len, int task_stack, int task_prio);

/* dodaj żądanie do kolejki (kopiuje bufor TX i przygotowuje staging RX) */
bool services_i2c_submit(const i2c_req_t* req);

#ifdef __cplusplus
}
#endif
