#pragma once
#include "ports/errors.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nieprzezroczysty uchwyt do paska LED */
typedef struct led_strip_dev led_strip_dev_t;

/** Typy diod */
typedef enum {
    LED_STRIP_WS2812, 
    LED_STRIP_SK6812  
} led_type_t;

typedef struct {
    int        gpio_num;
    int        max_leds;
    led_type_t type;
    bool       use_dma; 
} led_strip_cfg_t;

/* Zmiana nazw funkcji na led_port_*, aby nie kolidowały z biblioteką Espressif */

port_err_t led_port_create(const led_strip_cfg_t* cfg, led_strip_dev_t** out_dev);

port_err_t led_port_set_pixel(led_strip_dev_t* dev, int index, uint8_t r, uint8_t g, uint8_t b);

port_err_t led_port_set_pixel_rgbw(led_strip_dev_t* dev, int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

port_err_t led_port_clear(led_strip_dev_t* dev);

port_err_t led_port_refresh(led_strip_dev_t* dev, uint32_t timeout_ms);

port_err_t led_port_delete(led_strip_dev_t* dev);

#ifdef __cplusplus
}
#endif
