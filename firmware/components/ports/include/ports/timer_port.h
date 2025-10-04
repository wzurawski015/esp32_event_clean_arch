#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ports/errors.h"

/** @defgroup ports_timer Timer Port (ports)
 *  @ingroup ports
 *  @brief Kontrakt one-shot timerów; callback wywoływany w kontekście taska.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timer_port timer_port_t;
typedef void (*timer_cb_t)(void *user);

typedef struct {
    timer_cb_t cb;   /**< Obowiązkowy callback. */
    void*      user; /**< Uchwyt użytkownika przekazywany do cb. */
} timer_cfg_t;

/** Tworzy one-shot timer. */
port_err_t timer_create(const timer_cfg_t* cfg, timer_port_t** out);
/** Uruchamia jednorazowy timeout za @p delay_us mikrosekund. */
port_err_t timer_start_oneshot(timer_port_t* t, uint64_t delay_us);
/** Anuluje timer (jeśli działa). */
port_err_t timer_cancel(timer_port_t* t);
/** Usuwa timer. */
port_err_t timer_delete(timer_port_t* t);

#ifdef __cplusplus
} /* extern "C" */
#endif
