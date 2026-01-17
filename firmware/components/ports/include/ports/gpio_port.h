#pragma once
#include "ports/errors.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_GPIO_MODE_DISABLE = 0,
    PORT_GPIO_MODE_INPUT,
    PORT_GPIO_MODE_OUTPUT,
    PORT_GPIO_MODE_OUTPUT_OD,      // Open Drain
    PORT_GPIO_MODE_INPUT_OUTPUT,
    PORT_GPIO_MODE_INPUT_OUTPUT_OD
} port_gpio_mode_t;

typedef enum {
    PORT_GPIO_PULL_OFF = 0,
    PORT_GPIO_PULL_UP,
    PORT_GPIO_PULL_DOWN,
    PORT_GPIO_PULL_UP_DOWN
} port_gpio_pull_t;

typedef enum {
    PORT_GPIO_INTR_DISABLE = 0,
    PORT_GPIO_INTR_POSEDGE,    // Rising edge
    PORT_GPIO_INTR_NEGEDGE,    // Falling edge
    PORT_GPIO_INTR_ANYEDGE,    // Both edges
    PORT_GPIO_INTR_LOW_LEVEL,  // Level low
    PORT_GPIO_INTR_HIGH_LEVEL  // Level high
} port_gpio_intr_t;

/**
 * @brief Konfiguruje pin GPIO.
 */
port_err_t gpio_port_config(int pin, port_gpio_mode_t mode, port_gpio_pull_t pull);

/**
 * @brief Ustawia poziom logiczny na wyjściu.
 */
port_err_t gpio_port_set_level(int pin, bool level);

/**
 * @brief Odczytuje poziom logiczny pinu.
 * @return 0 lub 1, lub kod błędu (<0).
 */
int gpio_port_get_level(int pin);

/**
 * @brief Przełącza stan wyjścia na przeciwny.
 */
port_err_t gpio_port_toggle(int pin);

/**
 * @brief Konfiguruje przerwanie na pinie.
 * @param isr_handler Handler wywoływany w kontekście ISR (IRAM).
 * @param arg Argument przekazywany do handlera (np. numer pinu lub struktura).
 */
port_err_t gpio_port_set_intr(int pin, port_gpio_intr_t intr_type, void (*isr_handler)(void*), void* arg);

/**
 * @brief Włącza/wyłącza przerwanie dla skonfigurowanego pinu.
 */
port_err_t gpio_port_intr_enable(int pin, bool enable);

#ifdef __cplusplus
}
#endif
