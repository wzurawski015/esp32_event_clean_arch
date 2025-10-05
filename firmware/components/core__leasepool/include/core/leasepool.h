#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t idx;   // index w tablicy slotów
    uint16_t gen;   // generacja (anti-ABA)
} lp_handle_t;

typedef struct {
    void*    ptr;   // adres danych
    uint32_t cap;   // pojemność bufora (LP_BUF_SIZE)
    uint32_t len;   // logiczna długość danych
} lp_view_t;

// Inicjalizacja puli
bool lp_init(void);

// Alokacja pustego bufora (nie blokuje). Jeśli want_len > cap, zwróci invalid.
lp_handle_t lp_alloc_try(uint32_t want_len);

// Ustaw finalną długość payloadu (publikacja danych)
bool lp_commit(lp_handle_t h, uint32_t len);

// Dostęp do danych (waliduje idx+gen)
bool lp_acquire(lp_handle_t h, lp_view_t* out);

// Zwiększ licznik referencji o n (robi to szyna zdarzeń przy fan-out)
bool lp_addref_n(lp_handle_t h, uint16_t n);

// Zmniejsz ref; gdy spadnie do 0 — slot wraca do puli (gen++)
void lp_release(lp_handle_t h);

// Warianty ISR-safe (krótkie sekcje krytyczne)
lp_handle_t lp_alloc_try_isr(uint32_t want_len);
bool        lp_commit_isr(lp_handle_t h, uint32_t len);
bool        lp_addref_n_isr(lp_handle_t h, uint16_t n);
void        lp_release_isr(lp_handle_t h);

// Metryki (na potrzeby CLI/diag)
typedef struct {
    uint16_t slots_total;
    uint16_t slots_free;
    uint32_t drops_alloc_fail;
} lp_stats_t;

void lp_get_stats(lp_stats_t* s);

// Stała do sprawdzania poprawności uchwytu
#define LP_INVALID_IDX ((uint16_t)0xFFFF)

// --- Pomocnicy: pack/unpack uchwytu do 32-bit (np. na m.a / m.b w ev_msg_t) ---
static inline uint32_t lp_pack_handle_u32(lp_handle_t h)
{
    return ((uint32_t)h.idx << 16) | (uint32_t)h.gen;
}

static inline lp_handle_t lp_unpack_handle_u32(uint32_t w)
{
    lp_handle_t h;
    h.idx = (uint16_t)(w >> 16);
    h.gen = (uint16_t)(w & 0xFFFFu);
    return h;
}

// Alias zgodny z użyciem w aktorach (np. app__demo_lcd)
#define lp_unpack_u32 lp_unpack_handle_u32

#ifdef __cplusplus
}
#endif
