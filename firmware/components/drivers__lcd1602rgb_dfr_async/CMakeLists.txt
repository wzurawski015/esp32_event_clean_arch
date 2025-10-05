idf_component_register(
  SRCS "lcd1602rgb_dfr_async.c"
  INCLUDE_DIRS "include"
  REQUIRES
    core__ev
    services__i2c
    esp_timer
    ports
)
