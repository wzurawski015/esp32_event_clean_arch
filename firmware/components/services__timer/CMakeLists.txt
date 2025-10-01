idf_component_register(
  SRCS "services_timer.c"
  INCLUDE_DIRS "include"
  REQUIRES core__ev esp_timer
)
