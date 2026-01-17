#include "services_internal_temp.h"
#include "ports/internal_temp_port.h"
#include "ports/wdt_port.h"
#include "ports/log_port.h"
#include "core_ev.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "SVC_ITEMP";

/* Wewnętrzne sygnały dla kolejki serwisu */
typedef enum {
    SIG_TICK = 1
} internal_sig_t;

static internal_temp_dev_t* s_dev   = NULL;
static const ev_bus_t* s_bus   = NULL;
static TaskHandle_t         s_task  = NULL;
static QueueHandle_t        s_queue = NULL;
static esp_timer_handle_t   s_timer = NULL;

/* --- Helpers --- */

/**
 * @brief Bezpieczna konwersja float -> uint32 (Type Punning).
 * Używamy memcpy, aby uniknąć naruszenia Strict Aliasing Rule.
 * To jest metoda zalecana przez standardy MISRA C w takich przypadkach.
 */
static inline uint32_t float_to_u32(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* --- Callbacks --- */

/**
 * @brief Callback timera - wykonuje się w kontekście przerwania (ISR).
 * NIE wykonujemy tu odczytu (I2C/ADC może być wolne/blokujące).
 * Jedynie wysyłamy sygnał do taska.
 */
static void timer_cb(void* arg) {
    (void)arg;
    internal_sig_t sig = SIG_TICK;
    /* Send to back, nie czekamy (0), from ISR */
    xQueueSendFromISR(s_queue, &sig, NULL);
}

/* --- Worker Task --- */

static void temp_worker(void* arg)
{
    (void)arg;
    internal_sig_t sig;

    /* 1. Rejestracja w Watchdogu (Safety) */
    wdt_add_self();

    for (;;) {
        /* 2. Czekaj na sygnał (Pure Event Driven) */
        /* Timeout 2000ms służy tylko jako heartbeat dla Watchdoga, jeśli timer wolniejszy */
        if (xQueueReceive(s_queue, &sig, pdMS_TO_TICKS(2000)) == pdTRUE) {
            
            /* 3. Reset Watchdoga (Praca) */
            wdt_reset();

            if (sig == SIG_TICK) {
                float temp = 0.0f;
                /* Odczyt przez abstrakcję portu */
                if (internal_temp_read(s_dev, &temp) == PORT_OK) {
                    /* Publikacja na szynę (Zero-Copy) */
                    ev_bus_post(s_bus, EV_SRC_SYS, EV_SYS_TEMP_UPDATE, float_to_u32(temp), 0);
                } else {
                    LOGW(TAG, "Read failed");
                }
            }
        } else {
            /* 4. Reset Watchdoga (Idle) */
            wdt_reset();
        }
    }
}

/* --- Public API --- */

bool services_internal_temp_start(const ev_bus_t* bus, const internal_temp_svc_cfg_t* cfg)
{
    if (!bus || !cfg) return false;
    if (s_task) return true; // Już działa

    s_bus = bus;
    
    /* 1. Inicjalizacja Sprzętu */
    internal_temp_cfg_t pcfg = { .min_c = -10, .max_c = 80 };
    if (internal_temp_create(&pcfg, &s_dev) != PORT_OK) {
        LOGE(TAG, "Hardware init failed");
        return false;
    }

    /* 2. Kolejka sygnałów (tylko 1 slot potrzebny dla ticka, dajemy 4 na zapas) */
    s_queue = xQueueCreate(4, sizeof(internal_sig_t));
    if (!s_queue) return false;

    /* 3. Start Taska */
    /* Bardzo mały stos (2048), niski priorytet (1 - tło) */
    if (xTaskCreate(temp_worker, "svc_itemp", 2048, NULL, tskIDLE_PRIORITY + 1, &s_task) != pdPASS) {
        LOGE(TAG, "Task create failed");
        return false;
    }

    /* 4. Start Timera */
    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .name = "itemp_tick"
    };
    if (esp_timer_create(&timer_args, &s_timer) != ESP_OK) {
        LOGE(TAG, "Timer create failed");
        return false;
    }
    
    uint64_t period_us = (uint64_t)cfg->period_ms * 1000ULL;
    esp_timer_start_periodic(s_timer, period_us);

    LOGI(TAG, "Started (period: %d ms)", cfg->period_ms);
    return true;
}

void services_internal_temp_stop(void)
{
    if (s_timer) { esp_timer_stop(s_timer); esp_timer_delete(s_timer); s_timer = NULL; }
    // Graceful shutdown taska wymagałby dodatkowego sygnału, tu pomijamy dla uproszczenia
    if (s_dev) { internal_temp_delete(s_dev); s_dev = NULL; }
}

