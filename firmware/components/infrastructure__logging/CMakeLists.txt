# Komponent infrastruktury logowania.
# Źródła:
#  - logging_idf.c  (ring-buffer + hook do ESP log)
#  - logging_cli.c  (komenda 'logrb' i ewentualny REPL) – kompiluje się bezpiecznie,
#                    bo używa #if CONFIG_INFRA_LOG_CLI na sekcje zależne od esp_console.

idf_component_register(
    SRCS
        "logging_idf.c"
        "logging_cli.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        ports           # Twój port warstwy abstrakcji
        log             # IDF-owe esp_log
        console         # esp_console.h
        vfs             # esp_vfs_dev.h (REPL przez UART0)
        driver          # driver/uart.h
        core__ev        # core__ev/include i linkowanie
)
