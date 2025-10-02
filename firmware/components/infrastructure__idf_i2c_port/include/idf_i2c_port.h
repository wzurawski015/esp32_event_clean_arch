/**
 * @file idf_i2c_port.h
 * @brief Adapter I²C dla warstwy "infrastructure" oparty o nowy sterownik ESP‑IDF
 *        (driver/i2c_master.h). Nagłówek do użycia przez aplikację oraz komponenty.
 *
 *  - Standardowe typy i podstawowe API pochodzą z ports/i2c_port.h
 *  - Ten plik dodaje rozszerzenia specyficzne dla implementacji na ESP‑IDF,
 *    m.in. funkcję i2c_bus_probe_addr() do aktywnego sprawdzania obecności urządzenia.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Główny, wspólny interfejs portu I²C (typy i bazowe API) */
#include "ports/i2c_port.h"

/* --- Rozszerzenia specyficzne dla implementacji IDF --- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sprawdź, czy urządzenie pod adresem 7‑bit odpowiada (ACK).
 *
 * Tworzy sekwencję START + ADDRESS[W] + STOP bez danych. Przydaje się do
 * szybkiego skanowania magistrali lub auto‑wyboru adresów.
 *
 * @param[in]  bus         Wskaźnik na uchwyt magistrali (i2c_bus_t*).
 * @param[in]  addr7       Adres 7‑bit (0x00..0x7F).
 * @param[in]  timeout_ms  Timeout transakcji w milisekundach.
 * @param[out] out_ack     true → urządzenie odpowiedziało ACK, false → NACK.
 *
 * @return ESP_OK           – operacja wykonana poprawnie (wynik w out_ack),
 *         ESP_ERR_INVALID_ARG – niepoprawne argumenty,
 *         inny kod ESP_ERR_* – błąd warstwy I²C/sterownika.
 */
esp_err_t i2c_bus_probe_addr(i2c_bus_t* bus,
                             uint8_t    addr7,
                             uint32_t   timeout_ms,
                             bool*      out_ack);

#ifdef __cplusplus
}
#endif
