idf_component_register(
  SRCS "main.c"
  PRIV_REQUIRES core__ev services__i2c services__timer drivers__lcd1602rgb_dfr_async
  REQUIRES ports infrastructure__logging
)
