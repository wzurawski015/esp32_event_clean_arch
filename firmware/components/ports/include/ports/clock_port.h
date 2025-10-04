#pragma once
#include <stdint.h>

/** @defgroup ports_clock Clock Port (ports)
 *  @ingroup ports
 *  @brief Monotoniczny zegar w mikrosekundach (do timeoutów/pomiarów).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Zwraca monotoniczny czas w mikrosekundach od startu systemu. */
uint64_t clock_now_us(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
