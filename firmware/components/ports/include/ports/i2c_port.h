/**
 * @file i2c_port.h
 * @brief Abstrakcyjny interfejs magistrali I²C dla warstwy "ports".
 *
 * Warstwa ports nie zależy od ESP-IDF; na błędy używa port_err_t z ports/errors.h.
 * Implementację dla IDF dostarcza komponent infrastructure__idf_i2c_port.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ports/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque typy uchwytów – detale ukryte w implementacji. */
typedef struct i2c_bus i2c_bus_t;
typedef struct i2c_dev i2c_dev_t;

/** Konfiguracja utworzenia magistrali I²C. */
typedef struct {
    int      sda_gpio;                /**< Numer GPIO dla SDA. */
    int      scl_gpio;                /**< Numer GPIO dla SCL. */
    bool     enable_internal_pullup;  /**< true => włącz pull‑up (fallback). */
    uint32_t clk_hz;                  /**< Częstotliwość I²C (np. 100k, 400k). */
} i2c_bus_cfg_t;

/** Utworzenie/zwolnienie magistrali. */
port_err_t i2c_bus_create(const i2c_bus_cfg_t* cfg, i2c_bus_t** out_bus);
port_err_t i2c_bus_delete(i2c_bus_t* bus);

/** Dodanie/usunięcie urządzenia (7‑bitowy adres). */
port_err_t i2c_dev_add(i2c_bus_t* bus, uint8_t addr7, i2c_dev_t** out_dev);
port_err_t i2c_dev_remove(i2c_dev_t* dev);

/** Prymitywy I²C (zwykle używane przez services__i2c). */
port_err_t i2c_tx(i2c_dev_t* dev, const uint8_t* tx, size_t txlen, uint32_t timeout_ms);
port_err_t i2c_rx(i2c_dev_t* dev, uint8_t* rx, size_t rxlen, uint32_t timeout_ms);
port_err_t i2c_txrx(i2c_dev_t* dev,
                    const uint8_t* tx, size_t txlen,
                    uint8_t* rx, size_t rxlen,
                    uint32_t timeout_ms);

/**
 * @brief Sondowanie pojedynczego adresu 7‑bit.
 *
 * Semantyka:
 *  - Zwraca PORT_OK, a w @p out_ack wpisuje true, gdy urządzenie potwierdzi (ACK).
 *  - Zwraca PORT_OK, a w @p out_ack wpisuje false, gdy brak ACK (adres pusty).
 *  - Zwraca kod błędu (np. PORT_ERR_INVALID_ARG) tylko dla błędnych parametrów /
 *    błędów sterownika.
 */
port_err_t i2c_bus_probe_addr(i2c_bus_t* bus,
                              uint8_t addr7,
                              uint32_t timeout_ms,
                              bool* out_ack);

/**
 * @brief Skanowanie zakresu adresów; wpisuje znalezione adresy do @p out_found.
 * @param bus   Uchwyt magistrali utworzony przez i2c_bus_create().
 * @param start  Pierwszy adres (zalecane >= 0x03)
 * @param end    Ostatni adres (zalecane <= 0x77)
 * @param timeout_ms  Timeout pojedynczej próby
 * @param out_found   Opcjonalny bufor na adresy (może być NULL)
 * @param cap         Pojemność bufora out_found
 * @param out_n       Zwrócona liczba znalezionych adresów (może być NULL)
 */
port_err_t i2c_bus_scan_range(i2c_bus_t* bus,
                              uint8_t start,
                              uint8_t end,
                              uint32_t timeout_ms,
                              uint8_t* out_found,
                              size_t cap,
                              size_t* out_n);

#ifdef __cplusplus
}
#endif
