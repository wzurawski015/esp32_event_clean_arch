idf_component_register(
  SRCS "idf_i2c_port.c"
  INCLUDE_DIRS "include"
  REQUIRES ports
  PRIV_REQUIRES driver esp_driver_i2c
)
