# Komponent infrastruktury logowania.
# Źródła:
#  - logging_idf.c  (ring-buffer + hook do ESP log)
#  - logging_cli.c  (komendy CLI / REPL) – sekcje zależne od esp_console są #if CONFIG_INFRA_LOG_CLI

idf_component_register(
    SRCS
        "logging_idf.c"
        "logging_cli.c"
        "infra_log_stream.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        ports           # Twój port warstwy abstrakcji (log_port.h)
        log             # IDF-owe esp_log
        console         # esp_console.h
        vfs             # esp_vfs_dev.h (REPL przez UART0)
        esp_driver_uart # nowy sterownik UART (nagłówek: driver/uart.h)
        core__ev        # event bus: evstat + API
        core__spsc_ring # STREAM: SPSC ring-buffer (claim/commit)
        core__leasepool  # lpstat
)
