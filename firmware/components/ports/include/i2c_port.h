/**
 * @file i2c_port.h
 * @brief Abstrakcyjny interfejs magistrali I²C dla warstwy "ports" (Clean Architecture).
 *
 * Ten nagłówek definiuje *czysto abstrahowane* API na potrzeby warstw wyższych
 * (services / drivers). Prawdziwa implementacja znajduje się w komponencie
 * **infrastructure__idf_i2c_port**, który opiera się na **nowym** sterowniku
 * ESP‑IDF `driver/i2c_master.h`.
 *
 * @dot
 * digraph G {
 *   rankdir=LR; node [shape=box, fontname="Helvetica"];
 *   app     [label="projects/<app> (app)"];
 *   drv     [label="drivers__*"];
 *   svc     [label="services__i2c"];
 *   ports   [label="ports::i2c_port (to API)"];
 *   infra   [label="infrastructure__idf_i2c_port\n(driver/i2c_master.h)"];
 *   hw      [label="I2C HW", shape=circle];
 *   app  -> drv -> svc -> ports -> infra -> hw;
 * }
 * @enddot
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Opaque typy uchwytów – ukrywamy detale implementacji. */
    typedef struct i2c_bus i2c_bus_t;
    typedef struct i2c_dev i2c_dev_t;

    /** Konfiguracja utworzenia magistrali I²C. */
    typedef struct
    {
        int      sda_gpio;               /**< Numer GPIO dla SDA. */
        int      scl_gpio;               /**< Numer GPIO dla SCL. */
        bool     enable_internal_pullup; /**< true => włącz pull‑up wpadkowy (jeśli potrzebne). */
        uint32_t clk_hz;                 /**< Częstotliwość I²C (np. 100k, 400k). */
    } i2c_bus_cfg_t;

    /** Utwórz/zwolnij magistralę. */
    esp_err_t i2c_bus_create(const i2c_bus_cfg_t* cfg, i2c_bus_t** out_bus);
    esp_err_t i2c_bus_delete(i2c_bus_t* bus);

    /** Dodaj/usuń urządzenie (7‑bitowy adres). */
    esp_err_t i2c_dev_add(i2c_bus_t* bus, uint8_t addr7, i2c_dev_t** out_dev);
    esp_err_t i2c_dev_remove(i2c_dev_t* dev);

    /**
     * @name Przeniesione prymitywy I²C
     * @note Zwykle nie wołasz ich z aplikacji – są kolejkowane przez services__i2c.
     * @{
     */
    esp_err_t i2c_tx(i2c_dev_t* dev, const uint8_t* tx, size_t txlen, uint32_t timeout_ms);
    esp_err_t i2c_rx(i2c_dev_t* dev, uint8_t* rx, size_t rxlen, uint32_t timeout_ms);
    esp_err_t i2c_txrx(i2c_dev_t* dev, const uint8_t* tx, size_t txlen, uint8_t* rx, size_t rxlen, uint32_t timeout_ms);
    /** @} */

    /**
     * @brief Sprawdź, czy urządzenie o adresie 7‑bitowym odpowiada (ACK).
     *
     * @param bus         Magistrala I²C.
     * @param addr7       Adres 7‑bitowy (0x03..0x77).
     * @param timeout_ms  Timeout transakcji.
     * @param out_ack     true -> ACK, false -> brak ACK.
     *
     * @return ESP_OK zawsze, chyba że wystąpi błąd magistrali (np. ESP_ERR_TIMEOUT).
     *         W przypadku braku ACK zwracamy ESP_OK i ustawiamy *out_ack=false.
     */
    esp_err_t i2c_bus_probe_addr(i2c_bus_t* bus, uint8_t addr7, uint32_t timeout_ms, bool* out_ack);

#ifdef __cplusplus
}
#endif
