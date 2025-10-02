#pragma once
/*
 * Implementacja portu I²C na bazie nowego sterownika ESP‑IDF (driver/i2c_master.h).
 * Ten nagłówek rozszerza interfejs "ports/i2c_port.h" o funkcje skanowania.
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Bazowy interfejs i typy pochodzą z komponentu "ports" */
#include "ports/i2c_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Szybkie sprawdzenie ACK na wskazanym 7‑bitowym adresie. */
esp_err_t i2c_bus_probe_addr(i2c_bus_t* bus, uint8_t addr7,
                             uint32_t timeout_ms, bool* out_ack);

/* Przeskanuj zakres [start..end]; zapisuje do out_found (max cap sztuk), zwraca
   liczbę znalezionych przez out_n (może być NULL). */
esp_err_t i2c_bus_scan_range(i2c_bus_t* bus,
                             uint8_t start, uint8_t end,
                             uint32_t timeout_ms,
                             uint8_t* out_found, size_t cap, size_t* out_n);

#ifdef __cplusplus
}
#endif
