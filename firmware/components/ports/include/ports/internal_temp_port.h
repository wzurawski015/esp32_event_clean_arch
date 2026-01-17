#pragma once
#include "ports/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle do urządzenia.
 */
typedef struct internal_temp_dev internal_temp_dev_t;

/**
 * @brief Konfiguracja zakresu pomiarowego.
 * Precyzja czujnika zależy od zakresu. Dla CPU typowe to -10..80.
 */
typedef struct {
    int min_c; 
    int max_c; 
} internal_temp_cfg_t;

/**
 * @brief Inicjalizuje i włącza wbudowany czujnik.
 */
port_err_t internal_temp_create(const internal_temp_cfg_t* cfg, internal_temp_dev_t** out_dev);

/**
 * @brief Pobiera aktualną temperaturę.
 * Operacja jest nieblokująca (odczyt rejestru).
 */
port_err_t internal_temp_read(internal_temp_dev_t* dev, float* out_celsius);

/**
 * @brief Wyłącza czujnik i zwalnia pamięć.
 */
port_err_t internal_temp_delete(internal_temp_dev_t* dev);

#ifdef __cplusplus
}
#endif

