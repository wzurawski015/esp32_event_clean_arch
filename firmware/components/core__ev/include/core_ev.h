/**
 * @file core_ev.h
 * @brief Prosty event-bus (broadcast, pub/sub) dla Clean Architecture.
 *
 * Każdy subskrybent posiada własną kolejkę (QueueHandle_t). Eventy są
 * "broadcastowane" do wszystkich zarejestrowanych kolejek.
 *
 * Architektura i zasady:
 *  - ev_init() inicjalizuje mutex i listę subskrybentów
 *  - ev_subscribe() rejestruje kolejkę odbiorczą
 *  - ev_post() rozsyła zdarzenie do wszystkich zarejestrowanych kolejek
 *  - brak alokacji dynamicznej w hot-path (poza konstrukcją kolejek po stronie klienta)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maksymalna liczba subskrybentów (możesz zwiększyć wg potrzeb). */
#ifndef EV_MAX_SUBS
#define EV_MAX_SUBS 16
#endif

/** Źródła zdarzeń (warstwa systemu) */
typedef enum {
    EV_SRC_SYS   = 1,  ///< Zdarzenia systemowe (start, itp.)
    EV_SRC_TIMER = 2,  ///< Tiki zegarowe
    EV_SRC_I2C   = 3,  ///< Wyniki operacji I2C (services__i2c)
    EV_SRC_LCD   = 4,  ///< Sterownik LCD
    EV_SRC_DS18  = 5,  ///< Serwis DS18B20
} ev_src_t;

/** Kody zdarzeń (per źródło) */
typedef enum {
    /* EV_SRC_SYS */
    EV_SYS_START      = 10,   ///< Aplikacja gotowa – komponenty mogą startować

    /* EV_SRC_TIMER */
    EV_TICK_100MS     = 100,
    EV_TICK_1S        = 101,

    /* EV_SRC_I2C */
    EV_I2C_DONE       = 200,  ///< Operacja I2C zakończona OK (a0=req*)
    EV_I2C_ERROR      = 201,  ///< Operacja I2C błąd (a0=req*, a1=esp_err_t)

    /* EV_SRC_LCD */
    EV_LCD_READY      = 300,  ///< LCD zainicjalizowany
    EV_LCD_UPDATED    = 301,  ///< Bufor wypchnięty
    EV_LCD_ERROR      = 399,

    /* EV_SRC_DS18 */
    EV_DS18_READY     = 400,  ///< a0=(uintptr_t)int*(miliC) | float* – wg implementacji
    EV_DS18_ERROR     = 401,  ///< a0=kod błędu
} ev_code_t;

/** Komunikat event-busa */
typedef struct {
    uint16_t src;       ///< ev_src_t
    uint16_t code;      ///< ev_code_t
    uintptr_t a0;       ///< argument 0 (np. wskaźnik na strukturę)
    uintptr_t a1;       ///< argument 1 (np. kod błędu)
    uint32_t  t_ms;     ///< znacznik czasu (ms od startu)
} ev_msg_t;

/** Uchwyt kolejki subskrybenta */
typedef QueueHandle_t ev_queue_t;

/** Inicjalizacja event-busa. */
void ev_init(void);

/**
 * Subskrypcja – tworzysz własną kolejkę i rejestrujesz ją w ev-busie.
 * @param[out] out_queue – zwracany uchwyt kolejki (utworzony w tej funkcji)
 * @param[in]  queue_len – długość kolejki (liczba wiadomości)
 * @return true jeżeli OK
 */
bool ev_subscribe(ev_queue_t *out_queue, size_t queue_len);

/**
 * Rejestracja własnej kolejki (jeżeli sam już utworzyłeś QueueHandle_t).
 * @param q istniejąca kolejka
 * @return true jeżeli OK
 */
bool ev_register_queue(ev_queue_t q);

/** Wypis zdarzenia do wszystkich subskrybentów (non-blocking: 0-tick send). */
bool ev_post(uint16_t src, uint16_t code, uintptr_t a0, uintptr_t a1);

#ifdef __cplusplus
}
#endif
