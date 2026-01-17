idf_component_register(
    SRCS "idf_gpio_port.c"
    INCLUDE_DIRS "."
    REQUIRES ports
    PRIV_REQUIRES esp_driver_gpio
)
