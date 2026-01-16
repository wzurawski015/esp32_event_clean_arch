#pragma once
#include "ports/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicjalizuje systemowy Watchdog (TWDT).
 * @param timeout_ms Czas w ms, po którym nastąpi reset, jeśli task nie odpowie.
 * Zalecane: 3000..10000 ms.
 */
port_err_t wdt_init(uint32_t timeout_ms);

/**
 * @brief Rejestruje bieżący task w systemie Watchdoga.
 * Od tego momentu task MUSI wołać wdt_reset() częściej niż timeout.
 */
port_err_t wdt_add_self(void);

/**
 * @brief Sygnalizuje "życie" taska (głaskanie psa).
 */
port_err_t wdt_reset(void);

/**
 * @brief Wyrejestrowuje bieżący task (np. przed usunięciem taska).
 */
port_err_t wdt_remove_self(void);

#ifdef __cplusplus
}
#endif

