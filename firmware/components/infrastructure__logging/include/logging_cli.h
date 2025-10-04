/**
 * @file logging_cli.h
 * @brief Rejestracja komend CLI (logrb/loglvl) oraz start REPL (USB‑Serial‑JTAG lub UART).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Rejestruje komendy CLI (logrb / loglvl) w esp_console. */
void infra_log_cli_register(void);

/** Startuje REPL:
 *  - jeśli w Kconfig włączone CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y → USB‑Serial‑JTAG,
 *  - w przeciwnym razie fallback na UART0.
 *  Gdy CONFIG_INFRA_LOG_CLI_START_REPL=n, funkcja rejestruje tylko komendy (bez REPL).
 */
void infra_log_cli_start_repl(void);

#ifdef __cplusplus
}
#endif
