#include "ports/gpio_port.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_attr.h" // IRAM_ATTR

// Flaga singletona dla serwisu przerwań
static bool s_isr_service_installed = false;

static port_err_t map_err(esp_err_t err) {
    return (err == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t gpio_port_config(int pin, port_gpio_mode_t mode, port_gpio_pull_t pull){
    if (pin < 0) return PORT_ERR_INVALID_ARG;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .intr_type = GPIO_INTR_DISABLE, // Przerwania konfigurujemy osobno
    };

    switch (mode) {
        case PORT_GPIO_MODE_INPUT:          cfg.mode = GPIO_MODE_INPUT; break;
        case PORT_GPIO_MODE_OUTPUT:         cfg.mode = GPIO_MODE_OUTPUT; break;
        case PORT_GPIO_MODE_OUTPUT_OD:      cfg.mode = GPIO_MODE_OUTPUT_OD; break;
        case PORT_GPIO_MODE_INPUT_OUTPUT:   cfg.mode = GPIO_MODE_INPUT_OUTPUT; break;
        case PORT_GPIO_MODE_INPUT_OUTPUT_OD:cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD; break;
        default:                            cfg.mode = GPIO_MODE_DISABLE; break;
    }

    switch (pull) {
        case PORT_GPIO_PULL_UP:      cfg.pull_up_en = 1; cfg.pull_down_en = 0; break;
        case PORT_GPIO_PULL_DOWN:    cfg.pull_up_en = 0; cfg.pull_down_en = 1; break;
        case PORT_GPIO_PULL_UP_DOWN: cfg.pull_up_en = 1; cfg.pull_down_en = 1; break;
        default:                     cfg.pull_up_en = 0; cfg.pull_down_en = 0; break;
    }

    return map_err(gpio_config(&cfg));
}

port_err_t gpio_port_set_level(int pin, bool level) {
    return map_err(gpio_set_level((gpio_num_t)pin, level ? 1 : 0));
}

// IRAM_ATTR dla bezpieczeństwa w ISR (nawet przy zapisie do Flash)
int IRAM_ATTR gpio_port_get_level(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

port_err_t gpio_port_toggle(int pin) {
    int lvl = gpio_get_level((gpio_num_t)pin);
    return map_err(gpio_set_level((gpio_num_t)pin, !lvl));
}

port_err_t gpio_port_set_intr(int pin, port_gpio_intr_t intr_type, void (*isr_handler)(void*), void* arg){
    if (pin < 0) return PORT_ERR_INVALID_ARG;

    // 1. Instalacja serwisu ISR (Singleton/Lazy Init)
    if (!s_isr_service_installed) {
        // 0 = default flags
        esp_err_t err = gpio_install_isr_service(0); 
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_isr_service_installed = true;
        } else {
            return map_err(err);
        }
    }

    // 2. Ustawienie typu przerwania
    gpio_int_type_t it;
    switch (intr_type) {
        case PORT_GPIO_INTR_POSEDGE:   it = GPIO_INTR_POSEDGE; break;
        case PORT_GPIO_INTR_NEGEDGE:   it = GPIO_INTR_NEGEDGE; break;
        case PORT_GPIO_INTR_ANYEDGE:   it = GPIO_INTR_ANYEDGE; break;
        case PORT_GPIO_INTR_LOW_LEVEL: it = GPIO_INTR_LOW_LEVEL; break;
        case PORT_GPIO_INTR_HIGH_LEVEL:it = GPIO_INTR_HIGH_LEVEL; break;
        default:                       it = GPIO_INTR_DISABLE; break;
    }
    
    esp_err_t err = gpio_set_intr_type((gpio_num_t)pin, it);
    if (err != ESP_OK) return map_err(err);

    // 3. Rejestracja handlera
    if (isr_handler) {
        err = gpio_isr_handler_add((gpio_num_t)pin, isr_handler, arg);
        if (err != ESP_OK) return map_err(err);
    } else {
        gpio_isr_handler_remove((gpio_num_t)pin);
    }

    return PORT_OK;
}

port_err_t gpio_port_intr_enable(int pin, bool enable) {
    if (enable) {
        return map_err(gpio_intr_enable((gpio_num_t)pin));
    } else {
        return map_err(gpio_intr_disable((gpio_num_t)pin));
    }
}
