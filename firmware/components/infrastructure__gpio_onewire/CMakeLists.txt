idf_component_register(
    SRCS "gpio_onewire.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ports esp_driver_gpio esp_driver_rmt esp_rom
)
