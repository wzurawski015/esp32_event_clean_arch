/**
 * @file services_i2c.h
 * @brief Asynchroniczny serwis I²C – kolejkuje operacje i publikuje zdarzenia po zakończeniu.
 *
 * Serwis realizuje *worker* obsługujący magistralę bez blokowania tasków aplikacyjnych:
 * - API przyjmuje żądania (TX/RX/TXRX) i wrzuca je do kolejki,
 * - worker wykonuje transfery w tle, po każdym publikuje EV_I2C_DONE (przez core__ev),
 * - dzięki temu wyższe warstwy (np. sterownik LCD) działają czysto **event-driven**.
 *
 * @dot
 * digraph G {
 *   rankdir=LR; node [shape=box, fontname="Helvetica"];
 *   drv   [label="driver (np. LCD)"];
 *   svc   [label="services__i2c\n(worker, queue)"];
 *   port  [label="ports::i2c_port"];
 *   infra [label="infrastructure__idf_i2c_port"];
 *   drv -> svc -> port -> infra;
 *   svc -> drv [label="EV_I2C_DONE", color="blue"];
 * }
 * @enddot
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ports/i2c_port.h"

/* typy FreeRTOS użyte w API (priorytety, uchwyty) */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Rodzaj operacji. */
typedef enum { I2C_OP_TX, I2C_OP_RX, I2C_OP_TXRX } i2c_op_t;

/** Pojedyncze żądanie I²C kierowane do worker’a. */
typedef struct i2c_req {
    i2c_op_t     op;          /**< Operacja TX/RX/TXRX. */
    i2c_dev_t*   dev;         /**< Urządzenie docelowe. */
    const uint8_t* tx; size_t txlen; /**< Bufor nadawczy (może być NULL dla RX). */
    uint8_t*     rx; size_t rxlen;   /**< Bufor odbiorczy (może być 0 dla TX). */
    uint32_t     timeout_ms;  /**< Budżet czasu na transakcję. */
    void*        user;        /**< Przezroczysty kontekst dla użytkownika. */
} i2c_req_t;

/**
 * @brief Start serwisu I²C.
 * @param queue_len      Pojemność kolejki żądań.
 * @param worker_stack   Rozmiar stosu taska.
 * @param worker_prio    Priorytet taska.
 * @return true po sukcesie.
 */
bool services_i2c_start(size_t queue_len, uint32_t worker_stack, UBaseType_t worker_prio);

/**
 * @brief Dodaj żądanie I²C do kolejki (nie kopiuje buforów danych).
 * @return true jeśli żądanie przyjęte.
 */
bool services_i2c_submit(const i2c_req_t* req);

#ifdef __cplusplus
}
#endif
