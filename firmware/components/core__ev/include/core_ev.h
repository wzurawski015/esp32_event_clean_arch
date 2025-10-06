/**
 * @file core_ev.h
 * @brief Lekki, zdarzeniowy „event bus” oparty o FreeRTOS Queue (Clean Architecture).
 *
 * Architektura:
 *  - Producent woła ev_post(...) / ev_post_lease(...) / ev_post_from_isr(...).
 *  - Każdy aktor/subskrybent ma własną kolejkę (ev_subscribe()).
 *  - Fan‑out realizowany jest przez broadcast bez blokowania producenta.
 *  - Wariant „lease” daje zero‑copy payload z kontrolowanym refcountingiem.
 *
 * Warstwy:
 *  - core__*  – reguły (ten moduł)
 *  - ports    – adaptery do IDF
 *  - services – procesy/maszyny stanów
 *  - drivers  – sprzęt/urządzenia
 *  - app      – kompozycja (use‑case’y)
 */

#pragma once

#include "sdkconfig.h"   // CONFIG_CORE_EV_MAX_SUBS
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// FreeRTOS kolejki (nośnik eventów)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Lease (zero‑copy payload)
#include "core/leasepool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== KONFIGURACJA (jedno źródło prawdy) ===================== */
/* Jeśli ustawisz w Kconfig: CONFIG_CORE_EV_MAX_SUBS, to ta wartość wygra.
 * Inaczej domyślnie 12. */
#ifndef EV_MAX_SUBS
#  ifdef CONFIG_CORE_EV_MAX_SUBS
#    define EV_MAX_SUBS CONFIG_CORE_EV_MAX_SUBS
#  else
#    define EV_MAX_SUBS 12
#  endif
#endif

/* ===================== ŹRÓDŁA i KODY ZDARZEŃ (rozszerzalne) ===================== */
typedef uint16_t ev_src_t;

enum {
    EV_SRC_SYS   = 0x01,
    EV_SRC_TIMER = 0x02,
    EV_SRC_I2C   = 0x03,
    EV_SRC_LCD   = 0x04,
    EV_SRC_DS18  = 0x05,
    EV_SRC_LOG   = 0x06,   // strumień logów (mostek vprintf->EV)
};

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

/* ===================== RAMKA ZDARZENIA ===================== */
typedef struct {
    ev_src_t  src;     ///< Id źródła (EV_SRC_xxx)
    uint16_t  code;    ///< Kod w ramach źródła (np. EV_TICK_1S)
    uint32_t  a0;      ///< Payload #0 (np. zapakowany lease handle)
    uint32_t  a1;      ///< Payload #1 (np. długość/błąd)
    uint32_t  t_ms;    ///< Znacznik czasu (ms od startu)
} ev_msg_t;

/** Kolejka subskrybenta (własność aktora). */
typedef QueueHandle_t ev_queue_t;

/* ===================== API: LIFECYCLE i SUBSKRYPCJE ===================== */

void ev_init(void);

/**
 * @brief Tworzy kolejkę i dopina subskrybenta do busa.
 * @param[out] out_q Zwracana kolejka subskrybenta.
 * @param[in]  depth Głębokość kolejki (0 => domyślnie 8).
 * @return true jeśli dopięto, false gdy brak miejsca/zasobów.
 */
bool ev_subscribe(ev_queue_t* out_q, size_t depth);

/**
 * @brief Odłącza subskrybenta od busa (ustawia slot na NULL).
 *
 * @warning Ta funkcja **nie usuwa** kolejki (`vQueueDelete`), aby uniknąć
 *          wyścigu z trwającym broadcastem. Właściciel kolejki powinien
 *          zniszczyć ją dopiero po zatrzymaniu aktora (quiesce).
 */
bool ev_unsubscribe(ev_queue_t q);

/* ===================== API: PUBLIKACJA ZDARZEŃ ===================== */

/** @return true gdy trafiło do >=1 subskrybenta. */
bool ev_post(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);

/**
 * @brief Broadcast z payloadem LEASE (zero‑copy, refcount).
 *
 * Bus policzy fan‑out i wywoła lp_addref_n(h, fanout),
 * a następnie ZAWSZE zwolni ref producenta: lp_release(h).
 */
bool ev_post_lease(ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len);

/**
 * @brief Publikacja zdarzenia z **kontekstu ISR** (xQueueSendFromISR).
 * @return true gdy trafiło do >=1 subskrybenta.
 */
bool ev_post_from_isr(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);

/* ===================== STATYSTYKI ===================== */
typedef struct {
    uint16_t subs;          ///< ilu subskrybentów
    uint32_t posts_ok;      ///< ile ev_post* dotarło do >=1 suba
    uint32_t posts_drop;    ///< ile ev_post* do nikogo nie dotarło
    uint32_t enq_fail;      ///< ile enqueue nie weszło (kolejka pełna)
    uint16_t q_depth_max;   ///< największa głębokość dowolnej kolejki
} ev_stats_t;

void ev_get_stats(ev_stats_t* out);

#ifdef __cplusplus
}
#endif
