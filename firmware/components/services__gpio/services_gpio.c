#include "services_gpio.h"
#include "ports/gpio_port.h"
#include "ports/clock_port.h"
#include "esp_attr.h" // IRAM_ATTR
#include <stddef.h>

#define MAX_GPIO_INPUTS 8

typedef struct {
    int      pin;
    uint32_t debounce_us;
    uint64_t last_irq_time;
    const ev_bus_t* bus;
    bool     registered;
} gpio_ctx_t;

static gpio_ctx_t s_inputs[MAX_GPIO_INPUTS];

// ISR Handler: musi być w IRAM, żeby działał szybko i bezpiecznie
static void IRAM_ATTR gpio_isr_handler(void* arg){
    gpio_ctx_t* ctx = (gpio_ctx_t*)arg;
    if (!ctx) return;

    uint64_t now = clock_now_us();
    
    // Prosty debounce czasowy w ISR
    if (now - ctx->last_irq_time >= ctx->debounce_us) {
        ctx->last_irq_time = now;
        
        // Odczyt stanu pinu (funkcja portu musi być IRAM safe!)
        int level = gpio_port_get_level(ctx->pin);
        
        // Publikacja zdarzenia z ISR
        // EV_SRC_GPIO (definicja w core_ev.h)
        // a0 = pin, a1 = level
        ev_bus_post_from_isr(ctx->bus, EV_SRC_GPIO, EV_GPIO_INPUT, (uint32_t)ctx->pin, (uint32_t)level);
    }
}

void services_gpio_init(void) {
    for(int i=0; i<MAX_GPIO_INPUTS; i++) s_inputs[i].registered = false;
}

bool services_gpio_add_input(const ev_bus_t* bus, const gpio_button_cfg_t* cfg){
    if (!bus || !cfg) return false;

    // Znajdź wolny slot
    gpio_ctx_t* ctx = NULL;
    for(int i=0; i<MAX_GPIO_INPUTS; i++) {
        if (!s_inputs[i].registered) {
            ctx = &s_inputs[i];
            break;
        }
    }
    if (!ctx) return false; // Brak miejsca

    // Konfiguracja kontekstu
    ctx->pin = cfg->pin;
    ctx->debounce_us = cfg->debounce_ms * 1000ULL;
    ctx->last_irq_time = 0;
    ctx->bus = bus;
    ctx->registered = true;

    // 1. Konfiguracja pinu
    port_gpio_pull_t pull = PORT_GPIO_PULL_OFF;
    if (cfg->pull_up && cfg->pull_down) pull = PORT_GPIO_PULL_UP_DOWN;
    else if (cfg->pull_up)             pull = PORT_GPIO_PULL_UP;
    else if (cfg->pull_down)           pull = PORT_GPIO_PULL_DOWN;

    if (gpio_port_config(cfg->pin, PORT_GPIO_MODE_INPUT, pull) != PORT_OK) {
        ctx->registered = false;
        return false;
    }

    // 2. Ustawienie przerwania na oba zbocza (change)
    if (gpio_port_set_intr(cfg->pin, PORT_GPIO_INTR_ANYEDGE, gpio_isr_handler, ctx) != PORT_OK) {
        ctx->registered = false;
        return false;
    }
    
    // 3. Włączenie przerwania
    gpio_port_intr_enable(cfg->pin, true);

    return true;
}
