#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ports/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file kv_port.h
 * @brief Abstrakcja magazynu Key-Value (NVS).
 *
 * Umożliwia zapis/odczyt konfiguracji i stanów (int, string, blob)
 * w pamięci nieulotnej. Obsługuje partycje i namespace'y.
 */

/* Nieprzezroczysty uchwyt do otwartej przestrzeni nazw */
typedef struct kv_handle kv_handle_t;

/**
 * @brief Konfiguracja otwarcia przestrzeni nazw.
 */
typedef struct {
    const char* partition_name; /**< Nazwa partycji (np. "nvs"). NULL = domyślna. */
    const char* namespace_name; /**< Nazwa przestrzeni (max 15 znaków). Wymagane. */
    bool        read_only;      /**< Tryb tylko do odczytu (oszczędza pamięć/blokady). */
} kv_cfg_t;

/**
 * @brief Statystyki użycia pamięci NVS (Wear Leveling / Capacity).
 */
typedef struct {
    size_t used_entries;  /**< Liczba zajętych wpisów. */
    size_t free_entries;  /**< Liczba wolnych wpisów. */
    size_t total_entries; /**< Całkowita liczba wpisów dostępna dla NVS. */
    size_t namespace_count; /**< Liczba zdefiniowanych namespace'ów. */
} kv_stats_t;

/* --- Zarządzanie cyklem życia --- */

/**
 * @brief Otwiera przestrzeń nazw w NVS.
 * @param[in]  cfg         Konfiguracja (partycja, nazwa, tryb).
 * @param[out] out_handle  Wskaźnik na uchwyt.
 * @return PORT_OK w przypadku sukcesu.
 */
port_err_t kv_open(const kv_cfg_t* cfg, kv_handle_t** out_handle);

/**
 * @brief Zamyka uchwyt i zwalnia zasoby adaptera.
 * @note Nie usuwa danych z pamięci flash.
 */
void kv_close(kv_handle_t* handle);

/**
 * @brief Fizycznie zatwierdza zmiany w pamięci flash.
 * @note Wymagane w implementacji NVS po serii operacji set_*.
 */
port_err_t kv_commit(kv_handle_t* handle);

/* --- Zapis danych (Setters) --- */

port_err_t kv_set_int(kv_handle_t* h, const char* key, int32_t val);
port_err_t kv_set_string(kv_handle_t* h, const char* key, const char* val);

/**
 * @brief Zapisuje dane binarne (BLOB/Struct).
 * @param len Rozmiar danych w bajtach.
 */
port_err_t kv_set_blob(kv_handle_t* h, const char* key, const void* data, size_t len);

/* --- Odczyt danych (Getters) --- */

port_err_t kv_get_int(kv_handle_t* h, const char* key, int32_t* out_val);

/**
 * @brief Pobiera string.
 * @param[out] out_buf Bufor docelowy (może być NULL, aby tylko sprawdzić rozmiar).
 * @param[in]  cap     Pojemność bufora.
 * @param[out] out_len Rzeczywista długość stringa (bez null-terminatora).
 */
port_err_t kv_get_string(kv_handle_t* h, const char* key, char* out_buf, size_t cap, size_t* out_len);

/**
 * @brief Pobiera dane binarne.
 * @param[out] out_buf Bufor docelowy (może być NULL, aby tylko sprawdzić rozmiar).
 * @param[in]  cap     Pojemność bufora.
 * @param[out] out_len Rzeczywisty rozmiar bloba w pamięci.
 */
port_err_t kv_get_blob(kv_handle_t* h, const char* key, void* out_buf, size_t cap, size_t* out_len);

/* --- Usuwanie --- */

port_err_t kv_erase(kv_handle_t* h, const char* key);
port_err_t kv_erase_all(kv_handle_t* h);

/* --- Diagnostyka --- */

/**
 * @brief Pobiera statystyki partycji powiązanej z uchwytem.
 */
port_err_t kv_get_stats(kv_handle_t* h, kv_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

