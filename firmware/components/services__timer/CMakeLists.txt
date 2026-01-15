idf_component_register(
  SRCS "services_timer.c"
  INCLUDE_DIRS "include"
  REQUIRES core__ev ports infrastructure__idf_timer_port infrastructure__idf_clock_port
)
