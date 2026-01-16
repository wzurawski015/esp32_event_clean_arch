idf_component_register(
    SRCS "idf_nvs_port.c"
    INCLUDE_DIRS "."
    REQUIRES ports
    PRIV_REQUIRES nvs_flash log
)

