/**
 * @file logging_cli.h
 * @brief Rejestracja komend CLI i (opcjonalnie) start REPL.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Rejestruje komendy CLI („logrb”, „loglvl”) w sposób idempotentny.
 * Powtórne wywołanie zwraca ESP_OK i nic nie robi.
 */
esp_err_t infra_log_cli_register(void);

/**
 * @brief Startuje UART/USB REPL w sposób idempotentny/miękki (bez abortu).
 * Jeśli REPL już działa lub driver jest zajęty, zwraca ESP_OK.
 */
esp_err_t infra_log_cli_start_repl(void);

#ifdef __cplusplus
}
#endif
