#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Kod zdarzenia publikowanego przez most log->EV (bezpiecznie "zarezerwowany" zakres dla app_*)
enum { EV_LOG_NEW = 0x3100 };

/**
 * Start mostka log->EV.
 *
 * Na razie STUB: tylko loguje, że jest gotowy.
 * W kolejnym kroku podłączymy hook z loggera, który:
 *   - przy każdej nowej linii logu weźmie bufor z puli (lease),
 *   - skopiuje linię,
 *   - lp_commit(),
 *   - wyśle EV_LOG_NEW na szynę (z uchwytem jako payload).
 */
bool app_log_bus_start(void);

#ifdef __cplusplus
}
#endif
