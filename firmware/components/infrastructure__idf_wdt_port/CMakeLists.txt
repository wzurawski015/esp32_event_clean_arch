idf_component_register(
    SRCS "idf_wdt_port.c"
    INCLUDE_DIRS "."
    REQUIRES ports
    PRIV_REQUIRES esp_system  # Tu siedzi esp_task_wdt
)

