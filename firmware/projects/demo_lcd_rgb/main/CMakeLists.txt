idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        # --- 1. Core & Interfaces (Fundamenty) ---
        core__ev
        core__leasepool
        ports

        # --- 2. Services (Business Logic / Consumers) ---
        # UWAGA: Muszą być przed Infrastructure, aby Linker wiedział, 
        # że te symbole są wymagane (Consumer -> Provider).
        services__timer
        services__i2c
        services__uart
        services__led
        services__gpio
        services__internal_temp     # <--- KONSUMENT (Wymaga funkcji z portu)

        # --- 3. Infrastructure (Drivers / Providers) ---
        # Dostarczają implementację dla portów używanych przez serwisy.
        infrastructure__idf_i2c_port
        infrastructure__idf_uart_port
        infrastructure__idf_nvs_port
        infrastructure__idf_wdt_port
        infrastructure__idf_gpio_port
        infrastructure__idf_led_strip
        infrastructure__idf_internal_temp # <--- DOSTAWCA (Implementuje funkcje portu)

        # --- 4. Application Components & Actors ---
        drivers__lcd1602rgb_dfr_async
        infrastructure__logging
        app__log_bus
        app__demo_lcd

        # --- 5. External / System ---
        nvs_flash
)

# Opcjonalne linkowanie konsoli dla CLI
if(CONFIG_INFRA_LOG_CLI)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::console)
endif()
