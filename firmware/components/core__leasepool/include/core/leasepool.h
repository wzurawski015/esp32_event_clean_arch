#pragma once
#include <stdint.h>
#include <stdbool.h>

/**
 * @file leasepool.h
 * @brief Ultra‑lekki LeasePool (stałe sloty, ref‑count + generacje) dla payloadów LEASE.
 *
 * Założenia:
 *  - brak malloc/free w ścieżce krytycznej
 *  - stała liczba slotów o stałej pojemności (cap)
 *  - ref-count: slot zwalniany dopiero gdy refcnt spadnie do 0
 *  - generacja (gen) chroni przed użyciem starego uchwytu (stale handle)
 *
 * Uwaga: producent zazwyczaj:
 *  1) lp_alloc_try(want_len)
 *  2) lp_acquire() -> zapis do bufora
 *  3) lp_commit(len)
 *  4) publish: ev_post_lease(...)
 *
 * Konsument:
 *  1) lp_acquire() -> odczyt
 *  2) lp_release()
 */

typedef struct {
    uint16_t idx;  /* 0..N-1 */
    uint16_t gen;  /* generacja slotu */
} lp_handle_t;

typedef struct {
    void*    ptr;
    uint32_t len;
    uint32_t cap; /* stałe: LP_BUF_SIZE */
} lp_view_t;

typedef struct {
    uint16_t slots_total;
    uint16_t slots_free;
    uint16_t slots_used;
    uint16_t slots_peak_used;

    uint32_t alloc_ok;
    uint32_t drops_alloc_fail;

    /* Licznik naruszeń guardów (canary/poison/assert), jeśli włączone. */
    uint32_t guard_failures;
} lp_stats_t;

void      lp_init(void);

lp_handle_t lp_alloc_try(uint32_t want_len);
bool      lp_acquire(lp_handle_t h, lp_view_t* out);
void      lp_commit(lp_handle_t h, uint32_t len);

void      lp_addref_n(lp_handle_t h, uint16_t n);
void      lp_release(lp_handle_t h);

uint16_t  lp_free_count(void);
uint16_t  lp_used_count(void);
void      lp_get_stats(lp_stats_t* out);
void      lp_reset_stats(void);

/**
 * @brief Sprawdza integralność LeasePool (invariants, free‑list, canary/poison).
 *
 * @param verbose Jeśli true, wypisuje raport na stdout/ROM (w zależności od platformy).
 * @return liczba wykrytych problemów (0 == OK)
 */
int       lp_check(bool verbose);

/**
 * @brief Wypisuje tabelę slotów i free‑list (diagnostyka).
 */
void      lp_dump(void);

static inline lp_handle_t lp_invalid_handle(void) { return (lp_handle_t){ .idx = 0xFFFFu, .gen = 0u }; }
static inline bool        lp_handle_is_valid(lp_handle_t h) { return (h.idx != 0xFFFFu); }

static inline uint32_t lp_pack_handle_u32(lp_handle_t h)
{
    return ((uint32_t)h.idx & 0xFFFFu) | (((uint32_t)h.gen & 0xFFFFu) << 16);
}

static inline lp_handle_t lp_unpack_handle_u32(uint32_t v)
{
    return (lp_handle_t){ .idx = (uint16_t)(v & 0xFFFFu), .gen = (uint16_t)((v >> 16) & 0xFFFFu) };
}
