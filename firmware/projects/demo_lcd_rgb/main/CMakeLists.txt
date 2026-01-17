idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        core__ev
        core__leasepool
        ports
        services__timer
        services__i2c
        services__uart
        infrastructure__idf_i2c_port
        infrastructure__idf_uart_port
        infrastructure__idf_nvs_port
        infrastructure__idf_wdt_port  # <--- NOWOŚĆ: Port Watchdoga
        drivers__lcd1602rgb_dfr_async
        infrastructure__logging
        app__log_bus
        app__demo_lcd
        nvs_flash
        services__gpio
        infrastructure__idf_gpio_port
        services__led
        infrastructure__idf_led_strip
        infrastructure__idf_internal_temp
        services__internal_temp
)

if(CONFIG_INFRA_LOG_CLI)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::console)
endif()

