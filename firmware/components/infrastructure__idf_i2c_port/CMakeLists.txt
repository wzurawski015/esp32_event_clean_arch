idf_component_register(
  SRCS "idf_i2c_port.c"
  INCLUDE_DIRS "include"
  REQUIRES ports           # Publiczne API
  PRIV_REQUIRES
      esp_driver_i2c       # Nowy sterownik I2C
      esp_driver_gpio      # Do manualnego sterowania (bit-banging recovery)
      esp_rom              # Do esp_rom_delay_us
)
