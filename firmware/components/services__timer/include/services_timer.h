/**
 * @file services_timer.h
 * @brief Serwis timerów deadline-driven (one-shot / periodic) emitujący zdarzenia na EV-bus.
 *
 * Zamiast globalnych "ticków" (spam), serwis trzyma listę aktywnych deadline'ów i uzbraja
 * tylko najbliższy. Po wygaśnięciu publikuje wskazane zdarzenie (ev_post).
 *
 * Funkcje API są przeznaczone do użycia w kontekście taska (nie ISR).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Token (handle) timera.
 *
 * - 0 == nieprawidłowy / błąd
 * - pozostałe wartości są stabilne do czasu anulowania / wygaśnięcia
 */
typedef uint32_t services_timer_token_t;

/**
 * Uruchamia serwis timerów.
 *
 * @return true jeśli serwis działa (lub już działał).
 */
bool services_timer_start(void);

/**
 * Zatrzymuje serwis timerów i usuwa wszystkie aktywne deadline'y.
 */
void services_timer_stop(void);

/**
 * Uzbraja timer one-shot, który po @p delay_us opublikuje zdarzenie @p (src,code,a0,a1).
 *
 * @note Zdarzenie musi być typu EVK_NONE albo EVK_COPY.
 *
 * @return token != 0 w przypadku sukcesu.
 */
services_timer_token_t services_timer_arm_once_us(uint64_t delay_us,
                                                  ev_src_t src,
                                                  uint16_t code,
                                                  uint32_t a0,
                                                  uint32_t a1);

/**
 * Uzbraja timer periodyczny, który będzie publikował zdarzenie co @p period_us.
 * Pierwsze wyzwolenie następuje po @p period_us.
 *
 * @note Zdarzenie musi być typu EVK_NONE albo EVK_COPY.
 *
 * @return token != 0 w przypadku sukcesu.
 */
services_timer_token_t services_timer_arm_periodic_us(uint64_t period_us,
                                                      ev_src_t src,
                                                      uint16_t code,
                                                      uint32_t a0,
                                                      uint32_t a1);

/**
 * Anuluje timer.
 *
 * @return true jeśli timer został anulowany.
 */
bool services_timer_cancel(services_timer_token_t tok);

/**
 * Sprawdza czy token wskazuje aktywny timer.
 */
bool services_timer_is_active(services_timer_token_t tok);

#ifdef __cplusplus
}  // extern "C"
#endif
