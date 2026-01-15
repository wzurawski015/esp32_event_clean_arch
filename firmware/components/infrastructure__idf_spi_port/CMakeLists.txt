idf_component_register(
    SRCS "idf_spi_port.c"
    INCLUDE_DIRS "include"
    REQUIRES ports
    PRIV_REQUIRES esp_driver_spi log
)
