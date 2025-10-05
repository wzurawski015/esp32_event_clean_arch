/**
 * @file logging_cli.h
 * @brief Rejestracja komend CLI i (opcjonalnie) start REPL — wersja idempotentna.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * Rejestruje komendy CLI („logrb”, „loglvl”) w sposób idempotentny.
 * Powtórne wywołanie zwraca ESP_OK i nic nie robi.
 */
esp_err_t infra_log_cli_register(void);

/**
 * Startuje REPL (USB-Serial-JTAG jeśli włączone; inaczej UART) w sposób
 * idempotentny i „miękki”. Jeżeli REPL już działa albo driver UART
 * jest zajęty — zwraca ESP_OK bez abortu.
 */
esp_err_t infra_log_cli_start_repl(void);

#ifdef __cplusplus
}
#endif
