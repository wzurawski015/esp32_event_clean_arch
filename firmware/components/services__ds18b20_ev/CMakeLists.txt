idf_component_register(
  SRCS "services_ds18b20_ev.c"
  INCLUDE_DIRS "include"
  REQUIRES core__ev esp_timer
  PRIV_REQUIRES esp_driver_gpio
)
