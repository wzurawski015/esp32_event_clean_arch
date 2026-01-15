/**
 * @file services_ds18b20_ev.h
 * @brief Asynchroniczny serwis DS18B20 (single‑drop, 1‑Wire, bez blokad w logice).
 *
 * Klient otrzymuje zdarzenia:
 * - EV_DS18_READY (kind=LEASE, payload=ds18_result_t)
 * - EV_DS18_ERROR (a0 = kod błędu)
 */
#pragma once
#include <stdbool.h>
#include "core_ev.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Payload dla EV_DS18_READY */
    typedef struct {
        uint64_t rom_code; /* Unikalny ID czujnika (0 jeśli SKIP_ROM) */
        float    temp_c;   /* Temperatura w C */
    } ds18_result_t;

    typedef struct
    {
        int  gpio;
        int  resolution_bits; /* 9..12 */
        int  period_ms;       /* co ile wykonywać konwersje */
    } ds18_svc_cfg_t;

    /** Start serwisu. Zwraca true jeżeli udało się wystartować. */
    bool services_ds18_start(const ev_bus_t* bus, const ds18_svc_cfg_t* cfg);

    /** Stop serwisu (opcjonalny). */
    void services_ds18_stop(void);

#ifdef __cplusplus
}
#endif
