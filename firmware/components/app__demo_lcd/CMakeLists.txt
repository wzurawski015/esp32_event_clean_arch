idf_component_register(
  SRCS "app_demo_lcd.c"
  INCLUDE_DIRS "include"
  REQUIRES core__ev core__leasepool services__i2c drivers__lcd1602rgb_dfr_async infrastructure__idf_i2c_port ports
  PRIV_REQUIRES freertos
)
