/**
 * @file logging_cli.h
 * @brief Rejestracja komendy `logrb` i (opcjonalnie) start UART REPL.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Rejestruje komendę `logrb` w esp_console. */
void infra_log_cli_register(void);

/** @brief Startuje REPL UART (jeśli włączone w Kconfig); w przeciwnym razie no-op. */
void infra_log_cli_start_repl(void);

#ifdef __cplusplus
}
#endif
