# Infrastrukturę logowania budujemy zawsze.
# Jeśli włączysz CLI w Kconfig, dokładamy logging_cli.c + zależności (console, vfs, driver).

if(CONFIG_INFRA_LOG_CLI)
    idf_component_register(
        SRCS
            "logging_idf.c"
            "logging_cli.c"
        INCLUDE_DIRS
            "include"
        PRIV_INCLUDE_DIRS
            "."
        PRIV_REQUIRES
            ports
            log
            console     # esp_console.h
            vfs         # esp_vfs_dev.h
            driver      # driver/uart.h
    )
else()
    idf_component_register(
        SRCS
            "logging_idf.c"
        INCLUDE_DIRS
            "include"
        PRIV_INCLUDE_DIRS
            "."
        PRIV_REQUIRES
            ports
            log
    )
endif()
