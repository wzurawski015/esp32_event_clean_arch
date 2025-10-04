/**
 * @file logging_cli.h
 * @brief Rejestracja komend CLI i (opcjonalnie) start REPL.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Rejestruje komendy CLI (logrb / loglvl). */
void infra_log_cli_register(void);

/** Startuje REPL (USB-Serial-JTAG jeśli włączone w Kconfig; inaczej UART0). */
void infra_log_cli_start_repl(void);

#ifdef __cplusplus
}
#endif
