idf_component_register(
    SRCS "services_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES core__ev ports infrastructure__idf_clock_port
)
