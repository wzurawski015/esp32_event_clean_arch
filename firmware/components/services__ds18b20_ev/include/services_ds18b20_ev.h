/**
 * @file services_ds18b20_ev.h
 * @brief Asynchroniczny serwis DS18B20 (single‑drop, 1‑Wire, bez blokad w logice).
 *
 * Implementacja korzysta z bit‑banging 1‑Wire na wybranym GPIO oraz one‑shot esp_timer
 * do realizacji opóźnień konwersji (94/188/375/750 ms dla rozdzielczości 9..12 bit).
 * Klient otrzymuje zdarzenia:
 *   - EV_DS18_READY (a0 = (uintptr_t)(int)temp_millicelsius)
 *   - EV_DS18_ERROR (a0 = kod błędu)
 */
#pragma once
#include <stdbool.h>

#include "core_ev.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int  gpio;
        int  resolution_bits; /* 9..12 */
        int  period_ms;       /* co ile wykonywać konwersje */
        bool internal_pullup; /* true → włącz wewn. pull-up (zalecany i tak zewn. 4.7k) */
    } ds18_svc_cfg_t;

    /** Start serwisu. Zwraca true jeżeli udało się wystartować. */
    bool services_ds18_start(const ev_bus_t* bus, const ds18_svc_cfg_t* cfg);

    /** Stop serwisu (opcjonalny). */
    void services_ds18_stop(void);

#ifdef __cplusplus
}
#endif
