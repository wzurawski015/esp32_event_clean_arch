# Infrastrukturę logowania budujemy zawsze, CLI tylko gdy włączone w Kconfig.

set(srcs "logging_idf.c")
set(priv_reqs ports log)

if(CONFIG_INFRA_LOG_CLI)
    list(APPEND srcs "logging_cli.c")
    list(APPEND priv_reqs console)  # esp_console + REPL
endif()

idf_component_register(
    SRCS            ${srcs}
    INCLUDE_DIRS    "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES   ${priv_reqs}
)
