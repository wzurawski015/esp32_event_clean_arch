#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// FreeRTOS kolejki dla subskrybentów
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Lease (potrzebne do ev_post_lease)
#include "core/leasepool.h"

// Domyślny limit subskrybentów
#ifndef EV_MAX_SUBS
#define EV_MAX_SUBS 16
#endif

// Źródła zdarzeń (rozszerzalne)
typedef uint16_t ev_src_t;
enum {
    EV_SRC_SYS   = 0x01,
    EV_SRC_TIMER = 0x02,
    EV_SRC_I2C   = 0x03,
    EV_SRC_LCD   = 0x04,
    EV_SRC_DS18  = 0x05,
    EV_SRC_LOG   = 0x06,   // strumień logów (mostek vprintf->EV)
};

// Kody zdarzeń (zakresy per źródło)
enum {
    // SYS
    EV_SYS_START     = 0x0001,

    // TIMER
    EV_TICK_100MS    = 0x1000,
    EV_TICK_1S       = 0x1001,

    // I2C
    EV_I2C_DONE      = 0x2000,
    EV_I2C_ERROR     = 0x2001,

    // LCD
    EV_LCD_READY     = 0x3001,
    EV_LCD_UPDATED   = 0x3002,
    EV_LCD_ERROR     = 0x30FF,

    // DS18B20
    EV_DS18_READY    = 0x4000,
    EV_DS18_ERROR    = 0x4001,

    // LOG
    EV_LOG_NEW       = 0x5000, // kompletna linia logu (payload: lease)
};

// Ramka zdarzenia (payload: a0/a1; czas dla wygody)
typedef struct {
    ev_src_t  src;
    uint16_t  code;
    uint32_t  a0;     // ogólny payload (np. packed lease handle)
    uint32_t  a1;     // ogólny payload (np. length / err)
    uint32_t  t_ms;   // timestamp (ms) – nadawany przy ev_post/ev_post_lease
} ev_msg_t;

// Kolejka zdarzeń aktora (alias na QueueHandle_t)
typedef QueueHandle_t ev_queue_t;

// Inicjalizacja busa
void ev_init(void);

// Subskrypcja: tworzy kolejkę o 'depth' i dopina do busa
bool ev_subscribe(ev_queue_t* out_q, size_t depth);

// Broadcast "klasycznego" zdarzenia
bool ev_post(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);

// Broadcast zdarzenia z payloadem LEASE (zero-copy):
// - bus podbija refcnt o liczbę rzeczywiście dostarczonych subów,
// - ZAWSZE zdejmuje 1 referencję producenta,
// - jeśli n_delivered == 0 → lease zostaje zwolniony tutaj.
bool ev_post_lease(ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len);

// Proste metryki busa
typedef struct {
    uint16_t subs;          // ilu subskrybentów
    uint32_t posts_ok;      // ile ev_post* z ≥1 dostarczeniem
    uint32_t posts_drop;    // ile ev_post* bez żadnego dostarczenia
    uint32_t enq_fail;      // ile pojedynczych enqueue się nie zmieściło
    uint16_t q_depth_max;   // maksymalna głębokość kolejek subskrybentów
} ev_stats_t;
void ev_get_stats(ev_stats_t* out);

#ifdef __cplusplus
}
#endif
