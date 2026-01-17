idf_component_register(
    SRCS "idf_led_strip.c"
    INCLUDE_DIRS "."
    REQUIRES ports
    PRIV_REQUIRES esp_driver_gpio
    # Zależność 'led_strip' zostanie pobrana automatycznie dzięki idf_component.yml
)

