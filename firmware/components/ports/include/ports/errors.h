#pragma once
#include <stdint.h>

#ifdef ESP_PLATFORM
  #include "esp_err.h"
  typedef esp_err_t port_err_t;
  #define PORT_OK ESP_OK
  #define PORT_FAIL ESP_FAIL
  #define PORT_ERR_INVALID_ARG ESP_ERR_INVALID_ARG
#else
  /* Host build / testy jednostkowe bez ESP-IDF */
  typedef int32_t port_err_t;
  enum {
      PORT_OK = 0,
      PORT_FAIL = -1,
      PORT_ERR_INVALID_ARG = 0x102,
  };
#endif
