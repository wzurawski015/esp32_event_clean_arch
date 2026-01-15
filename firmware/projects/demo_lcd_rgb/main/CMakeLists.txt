idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        core__ev
        core__leasepool
        ports
        services__timer
        services__i2c
        services__uart                # +++ NOWOŚĆ: Serwis UART
        infrastructure__idf_i2c_port
        infrastructure__idf_uart_port # +++ NOWOŚĆ: Adapter UART
        drivers__lcd1602rgb_dfr_async
        infrastructure__logging
        app__log_bus
        app__demo_lcd
)

if(CONFIG_INFRA_LOG_CLI)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::console)
endif()
