# Warunkowe listy zależności dla CLI
set(EXTRA_REQS "")
set(EXTRA_PRIV_REQS "")
if(CONFIG_INFRA_LOG_CLI)
  list(APPEND EXTRA_REQS infrastructure__logging)  # nagłówki logging_cli.h
  list(APPEND EXTRA_PRIV_REQS console)             # runtime IDF konsoli (REPL/komendy)
endif()

idf_component_register(
  SRCS "app_demo_lcd.c"
  INCLUDE_DIRS "include"
  REQUIRES
    core__ev
    core__leasepool
    services__i2c
    drivers__lcd1602rgb_dfr_async
    infrastructure__idf_i2c_port
    ports
    ${EXTRA_REQS}
  PRIV_REQUIRES
    freertos
    ${EXTRA_PRIV_REQS}
)
