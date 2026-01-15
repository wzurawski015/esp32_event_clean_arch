idf_component_register(
  SRCS "idf_i2c_port.c"
  INCLUDE_DIRS "include"
  REQUIRES ports           # Publiczne API (bo Twój nagłówek includuje ports/i2c_port.h)
  PRIV_REQUIRES esp_driver_i2c  # Tylko konkretny sterownik, bez starego 'driver'
)
