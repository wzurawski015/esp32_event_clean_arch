idf_component_register(
  SRCS "services_i2c.c"
  INCLUDE_DIRS "include"
  REQUIRES core__ev ports infrastructure__idf_i2c_port
)
