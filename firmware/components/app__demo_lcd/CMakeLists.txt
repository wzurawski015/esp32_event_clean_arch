idf_component_register(
  SRCS
  "app_demo_lcd.c"
  INCLUDE_DIRS
  "include"
  REQUIRES
  core__ev
  core__leasepool
  services__i2c
  drivers__lcd1602rgb_dfr_async
  infrastructure__idf_i2c_port
  ports
  PRIV_REQUIRES
  freertos)

# Jeżeli używasz CLI do logów, dołącz odpowiednie komponenty
if(CONFIG_INFRA_LOG_CLI)
  # Nagłówki i kod CLI (logging_cli.h / infra_log_cli_*)
  idf_component_requires(${COMPONENT_LIB} infrastructure__logging)
  # Runtime IDF konsoli (REPL, komendy)
  target_link_libraries(${COMPONENT_LIB} PRIVATE idf::console)
endif()
