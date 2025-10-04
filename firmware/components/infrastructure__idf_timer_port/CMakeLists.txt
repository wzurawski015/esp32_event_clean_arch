idf_component_register(
  SRCS
  "timer_idf.c"
  INCLUDE_DIRS
  "."
  PRIV_REQUIRES
  ports
  esp_timer)
