#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Typy diod obsługiwane przez serwis (mapowane na porty).
 */
typedef enum {
    LED_SVC_WS2812,
    LED_SVC_SK6812
} led_svc_type_t;

/**
 * @brief Konfiguracja serwisu LED.
 */
typedef struct {
    int            gpio_num;
    int            max_leds;
    led_svc_type_t led_type;
} led_svc_cfg_t;

/**
 * @brief Uruchamia serwis LED.
 *
 * Serwis nasłuchuje na zdarzenia:
 * - EV_LED_SET_RGB (a0 = 0x00BBGGRR)
 * - EV_SYS_START (opcjonalna animacja startowa)
 *
 * @param bus Wskaźnik na magistralę zdarzeń.
 * @param cfg Konfiguracja sprzętowa.
 * @return true w przypadku sukcesu.
 */
bool services_led_start(const ev_bus_t* bus, const led_svc_cfg_t* cfg);

/**
 * @brief Zatrzymuje serwis (opcjonalne).
 */
void services_led_stop(void);

#ifdef __cplusplus
}
#endif

