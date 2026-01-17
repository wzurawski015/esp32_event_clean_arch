#pragma once
#include <stdbool.h>
#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int period_ms; // Częstotliwość próbkowania (np. 1000ms)
} internal_temp_svc_cfg_t;

/**
 * @brief Uruchamia serwis monitorowania temperatury układu.
 * * Działanie:
 * 1. Uruchamia timer programowy (High Resolution Timer).
 * 2. Timer cyklicznie wybudza taska serwisu.
 * 3. Task odczytuje temperaturę i publikuje EV_SYS_TEMP_UPDATE.
 */
bool services_internal_temp_start(const ev_bus_t* bus, const internal_temp_svc_cfg_t* cfg);

void services_internal_temp_stop(void);

#ifdef __cplusplus
}
#endif

