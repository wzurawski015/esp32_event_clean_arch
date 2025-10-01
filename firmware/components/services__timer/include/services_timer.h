/**
 * @file services_timer.h
 * @brief Serwis zegarów – generuje EV_TICK_100MS i EV_TICK_1S z użyciem esp_timer.
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Inicjalizacja serwisu zegarów. */
bool services_timer_start(void);

/** Zatrzymanie (opcjonalne). */
void services_timer_stop(void);

#ifdef __cplusplus
}
#endif
