idf_component_register(
    SRCS "idf_internal_temp.c"
    INCLUDE_DIRS "."
    REQUIRES ports
    PRIV_REQUIRES esp_driver_tsens log
)
