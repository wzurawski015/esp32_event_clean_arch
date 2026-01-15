idf_component_register(
    SRCS "idf_uart_port.c"
    INCLUDE_DIRS "include"
    REQUIRES ports
    PRIV_REQUIRES esp_driver_uart log
)
