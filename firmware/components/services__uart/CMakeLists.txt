idf_component_register(
    SRCS "services_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        core__ev 
        ports 
        infrastructure__idf_uart_port 
        core__leasepool
        infrastructure__idf_wdt_port  # <--- NOWOŚĆ: Watchdog
)

