/**
 * @file services_led.c
 * @brief Serwis sterowania diodą/paskiem LED w architekturze zdarzeniowej.
 *
 * Realizuje:
 * - Subskrypcję zdarzeń systemowych (EV_LED_SET_RGB, EV_SYS_START).
 * - Integrację z Watchdogiem (TWDT).
 * - Bezpieczne sterowanie sprzętem poprzez warstwę Ports.
 */

#include "services_led.h"
#include "ports/led_strip_port.h"
#include "ports/wdt_port.h"
#include "ports/log_port.h"
#include "core_ev.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char* TAG = "SVC_LED";

/* Wewnętrzny stan serwisu */
static led_strip_dev_t* s_strip = NULL;
static const ev_bus_t* s_bus   = NULL;
static ev_queue_t       s_q     = NULL;
static TaskHandle_t     s_task  = NULL;

/**
 * @brief Helper do rozpakowania koloru z eventu (format 0x00BBGGRR).
 */
static void unpack_rgb(uint32_t packed, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)(packed & 0xFF);
    *g = (uint8_t)((packed >> 8) & 0xFF);
    *b = (uint8_t)((packed >> 16) & 0xFF);
}

/**
 * @brief Główna pętla zadania (Worker Task).
 */
static void led_task(void* arg)
{
    (void)arg;
    ev_msg_t msg;

    /* 1. Rejestracja w Task Watchdog (wymagane w systemach krytycznych) */
    if (wdt_add_self() != PORT_OK) {
        LOGE(TAG, "Failed to register in WDT!");
        // Kontynuujemy, ale to poważny błąd systemowy
    }

    for (;;) {
        /* 2. Odbiór zdarzeń z timeoutem (Heartbeat pattern) */
        /* Timeout 1000ms gwarantuje, że task "zamelduje się" psu nawet przy braku pracy */
        if (xQueueReceive(s_q, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            /* 3. Reset WDT (Task przetwarza dane -> żyje) */
            wdt_reset();

            /* Obsługa zdarzeń */
            if (msg.code == EV_LED_SET_RGB) {
                uint8_t r, g, b;
                unpack_rgb(msg.a0, &r, &g, &b);

                /* Ustawienie piksela 0 (dla DevKitC-1 jest to wbudowana dioda) */
                /* W przyszłości: tu może być pętla po wszystkich diodach taśmy */
                led_port_set_pixel(s_strip, 0, r, g, b);
                
                /* Fizyczny transfer danych (RMT) */
                /* Timeout 100ms jest bezpieczny dla krótkich pasków */
                if (led_port_refresh(s_strip, 100) != PORT_OK) {
                    LOGW(TAG, "LED refresh failed");
                }
            }
            else if (msg.code == EV_SYS_START) {
                /* Startowa sekwencja (opcjonalna) */
                /* Np. mrugnięcie na niebiesko, że system wstał */
                led_port_set_pixel(s_strip, 0, 0, 0, 20); // Blue, low brightness
                led_port_refresh(s_strip, 100);
            }

            /* Opcjonalnie: logowanie nieobsłużonych zdarzeń w trybie debug */
            else {
                LOGD(TAG, "Ignored event: src=%04X code=%04X", msg.src, msg.code);
            }

        } else {
            /* 4. Reset WDT w stanie IDLE (Heartbeat) */
            wdt_reset();
        }
    }

    /* Sprzątanie (Unreachable w normalnym cyklu, ale dobra praktyka) */
    wdt_remove_self();
    vTaskDelete(NULL);
}

bool services_led_start(const ev_bus_t* bus, const led_svc_cfg_t* cfg)
{
    if (!bus || !cfg) return false;
    if (s_task) return true; // Idempotentność: już działa

    s_bus = bus;

    /* 1. Inicjalizacja Portu (Hardware Abstraction) */
    led_strip_cfg_t port_cfg = {
        .gpio_num = cfg->gpio_num,
        .max_leds = cfg->max_leds,
        .type     = (led_type_t)cfg->led_type,
        
        /* FIX: DMA wyłączone.
         * Powód: 
         * 1. Dla pojedynczej diody/krótkiej taśmy DMA to overkill (zużywa cenny kanał).
         * 2. Na ESP32-C6 driver RMT zgłasza "DMA not supported" dla niektórych konfiguracji.
         * Tryb przerwań (interrupt-driven) jest w pełni wystarczający i stabilny.
         */
        .use_dma  = false 
    };

    if (led_port_create(&port_cfg, &s_strip) != PORT_OK) {
        LOGE(TAG, "Port create failed");
        return false;
    }

    /* 2. Subskrypcja zdarzeń */
    if (!ev_bus_subscribe(s_bus, &s_q, 8)) { // Kolejka o głębokości 8 wystarczy dla LED
        LOGE(TAG, "Subscribe failed");
        led_port_delete(s_strip);
        return false;
    }

    /* 3. Start Workera */
    /* Stack 3072B jest bezpieczny, bo używamy printf/logowania */
    BaseType_t ret = xTaskCreate(led_task, "svc_led", 3072, NULL, tskIDLE_PRIORITY + 1, &s_task);
    if (ret != pdPASS) {
        LOGE(TAG, "Task create failed");
        // FIX: Poprawne użycie API ev_bus_unsubscribe (bus, queue)
        ev_bus_unsubscribe(s_bus, s_q);
        vQueueDelete(s_q);
        led_port_delete(s_strip);
        return false;
    }

    LOGI(TAG, "Service started on GPIO%d", cfg->gpio_num);
    return true;
}

void services_led_stop(void)
{
    // TODO: Graceful shutdown (vTaskDelete, wdt_remove, led_strip_delete)
    // W systemach embedded rzadko zatrzymujemy core services.
}

