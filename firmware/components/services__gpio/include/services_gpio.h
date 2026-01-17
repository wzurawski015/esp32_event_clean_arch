#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      pin;
    bool     active_low;     // true: 0=Active, false: 1=Active (dla info, event wysyła raw level)
    uint32_t debounce_ms;    // Czas debouncingu (np. 50ms)
    bool     pull_up;
    bool     pull_down;
} gpio_button_cfg_t;

/**
 * @brief Inicjalizuje pulę slotów serwisu GPIO.
 */
void services_gpio_init(void);

/**
 * @brief Dodaje monitorowany przycisk/wejście.
 * Serwis skonfiguruje pin, przerwanie i będzie publikował EV_GPIO_INPUT.
 * * Payload eventu (EV_GPIO_INPUT):
 * a0 = numer pinu
 * a1 = stan logiczny (0 lub 1) - surowy stan pinu w momencie przerwania
 */
bool services_gpio_add_input(const ev_bus_t* bus, const gpio_button_cfg_t* cfg);

#ifdef __cplusplus
}
#endif
