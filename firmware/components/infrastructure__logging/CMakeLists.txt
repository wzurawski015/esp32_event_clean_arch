# Infrastrukturę logowania budujemy zawsze, CLI tylko gdy włączone w Kconfig.

set(srcs "logging_idf.c")
set(priv_reqs ports log)

if(CONFIG_INFRA_LOG_CLI)
    list(APPEND srcs "logging_cli.c")
    # esp_console + REPL, dostęp do UART_NUM_0 i esp_vfs_dev.h
    list(APPEND priv_reqs console vfs driver)
endif()

idf_component_register(
    SRCS              ${srcs}
    INCLUDE_DIRS      "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES     ${priv_reqs}
)
