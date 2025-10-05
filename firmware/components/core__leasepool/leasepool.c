#include "core/leasepool.h"
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_attr.h"   // DMA_ATTR

#ifndef CONFIG_LP_NUM_SLOTS
#define CONFIG_LP_NUM_SLOTS 32
#endif
#ifndef CONFIG_LP_BUF_SIZE
#define CONFIG_LP_BUF_SIZE 256
#endif

#define LP_NUM_SLOTS CONFIG_LP_NUM_SLOTS
#define LP_BUF_SIZE  CONFIG_LP_BUF_SIZE

typedef struct {
    volatile uint16_t gen;
    volatile uint16_t refcnt;
    volatile uint32_t len;
    uint8_t buf[LP_BUF_SIZE] __attribute__((aligned(4)));
} lp_slot_t;

static lp_slot_t s_slots[LP_NUM_SLOTS] DMA_ATTR;
static uint16_t  s_free[LP_NUM_SLOTS];
static volatile uint16_t s_free_top; // stos wolnych slotów (liczba zajętych elementów)
static volatile uint32_t s_drops_alloc_fail;

#if defined(portMUX_INITIALIZER_UNLOCKED)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define CS_ENTER()      portENTER_CRITICAL(&s_mux)
#define CS_EXIT()       portEXIT_CRITICAL(&s_mux)
#ifdef portENTER_CRITICAL_ISR
  #define CS_ENTER_ISR()  portENTER_CRITICAL_ISR(&s_mux)
  #define CS_EXIT_ISR()   portEXIT_CRITICAL_ISR(&s_mux)
#else
  #define CS_ENTER_ISR()  portENTER_CRITICAL(&s_mux)
  #define CS_EXIT_ISR()   portEXIT_CRITICAL(&s_mux)
#endif
#else
#define CS_ENTER()      taskENTER_CRITICAL()
#define CS_EXIT()       taskEXIT_CRITICAL()
#define CS_ENTER_ISR()  CS_ENTER()
#define CS_EXIT_ISR()   CS_EXIT()
#endif

static inline bool lp_valid_idx(uint16_t idx) { return idx < LP_NUM_SLOTS; }
static inline bool lp_valid_handle(lp_handle_t h) {
    return lp_valid_idx(h.idx) && (s_slots[h.idx].gen == h.gen);
}

bool lp_init(void)
{
    CS_ENTER();
    s_free_top = 0;
    for (uint16_t i = 0; i < LP_NUM_SLOTS; ++i) {
        s_slots[i].gen    = 1;
        s_slots[i].refcnt = 0;
        s_slots[i].len    = 0;
        s_free[s_free_top++] = i;
    }
    s_drops_alloc_fail = 0;
    CS_EXIT();
    return true;
}

static inline lp_handle_t lp_alloc_try_impl(uint32_t want_len, bool isr)
{
    lp_handle_t h = { .idx = LP_INVALID_IDX, .gen = 0 };
    if (want_len > LP_BUF_SIZE) {
        return h; // payload większy niż slot -> invalid
    }

    if (!isr) { CS_ENTER(); } else { CS_ENTER_ISR(); }
    if (s_free_top == 0) {
        if (!isr) { CS_EXIT(); } else { CS_EXIT_ISR(); }
        s_drops_alloc_fail++;
        return h;
    }
    uint16_t idx = s_free[--s_free_top];
    s_slots[idx].refcnt = 1; // producent trzyma 1 ref do czasu rozgłoszenia
    s_slots[idx].len = 0;
    h.idx = idx;
    h.gen = s_slots[idx].gen;
    if (!isr) { CS_EXIT(); } else { CS_EXIT_ISR(); }
    return h;
}

lp_handle_t lp_alloc_try(uint32_t want_len)      { return lp_alloc_try_impl(want_len, false); }
lp_handle_t lp_alloc_try_isr(uint32_t want_len)  { return lp_alloc_try_impl(want_len, true);  }

static inline bool lp_commit_impl(lp_handle_t h, uint32_t len, bool isr)
{
    if (!lp_valid_handle(h)) return false;
    if (len > LP_BUF_SIZE)   len = LP_BUF_SIZE;

    __asm__ __volatile__ ("" ::: "memory"); // bariera przed publikacją len
    if (!isr) { CS_ENTER(); } else { CS_ENTER_ISR(); }
    s_slots[h.idx].len = len;
    if (!isr) { CS_EXIT(); } else { CS_EXIT_ISR(); }
    __asm__ __volatile__ ("" ::: "memory"); // bariera po publikacji len

    return true;
}

bool lp_commit(lp_handle_t h, uint32_t len)     { return lp_commit_impl(h, len, false); }
bool lp_commit_isr(lp_handle_t h, uint32_t len) { return lp_commit_impl(h, len, true);  }

bool lp_acquire(lp_handle_t h, lp_view_t* out)
{
    if (!out) return false;
    if (!lp_valid_handle(h)) return false;
    out->ptr = (void*)s_slots[h.idx].buf;
    out->cap = LP_BUF_SIZE;
    out->len = s_slots[h.idx].len;
    return true;
}

static inline bool lp_addref_n_impl(lp_handle_t h, uint16_t n, bool isr)
{
    if (!lp_valid_handle(h)) return false;
    if (!isr) { CS_ENTER(); } else { CS_ENTER_ISR(); }
    s_slots[h.idx].refcnt += n;
    if (!isr) { CS_EXIT(); } else { CS_EXIT_ISR(); }
    return true;
}

bool lp_addref_n(lp_handle_t h, uint16_t n)     { return lp_addref_n_impl(h, n, false); }
bool lp_addref_n_isr(lp_handle_t h, uint16_t n) { return lp_addref_n_impl(h, n, true);  }

static inline void lp_release_impl(lp_handle_t h, bool isr)
{
    if (!lp_valid_idx(h.idx)) return;
    if (!isr) { CS_ENTER(); } else { CS_ENTER_ISR(); }
    lp_slot_t* s = &s_slots[h.idx];
    if (s->gen == h.gen && s->refcnt > 0) {
        s->refcnt--;
        if (s->refcnt == 0) {
            s->len = 0;
            s->gen++;                 // anti-ABA
            s_free[s_free_top++] = h.idx; // oddaj slot na stos wolnych
        }
    }
    if (!isr) { CS_EXIT(); } else { CS_EXIT_ISR(); }
}

void lp_release(lp_handle_t h)     { lp_release_impl(h, false); }
void lp_release_isr(lp_handle_t h) { lp_release_impl(h, true);  }

void lp_get_stats(lp_stats_t* st)
{
    if (!st) return;
    CS_ENTER();
    st->slots_total = LP_NUM_SLOTS;
    st->slots_free  = s_free_top;
    CS_EXIT();
    st->drops_alloc_fail = s_drops_alloc_fail;
}
