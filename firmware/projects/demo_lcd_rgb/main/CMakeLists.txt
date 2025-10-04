idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        core__ev
        ports
        services__timer
        services__i2c
        infrastructure__idf_i2c_port
        drivers__lcd1602rgb_dfr_async
        infrastructure__logging
)

# (Opcjonalnie) jeśli używasz komendy 'logrb' (esp_console):
if(CONFIG_INFRA_LOG_CLI)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::console)
endif()
