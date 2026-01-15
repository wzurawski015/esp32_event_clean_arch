#include "sdkconfig.h"
#include "core/leasepool.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if ESP_PLATFORM
#include "esp_rom_sys.h" // esp_rom_printf
#endif

#define LP_NUM_SLOTS CONFIG_CORE_LEASEPOOL_SLOTS
#define LP_BUF_SIZE  CONFIG_CORE_LEASEPOOL_SLOT_BYTES

#if CONFIG_CORE_LEASEPOOL_GUARD
#define LP_CANARY_VALUE 0xC0DEF00Du
#define LP_MAGIC_FREE   0xFEE1DEADu
#define LP_MAGIC_USED   0xC0FFEE01u
#define LP_POISON_FREE  0xA5u
#define LP_POISON_ALLOC 0xCCu
#endif

#if ESP_PLATFORM
#define LP_DIAG_PRINTF(...) esp_rom_printf(__VA_ARGS__)
#else
#define LP_DIAG_PRINTF(...) printf(__VA_ARGS__)
#endif

typedef struct {
#if CONFIG_CORE_LEASEPOOL_GUARD
    volatile uint32_t canary_head;
    volatile uint32_t magic;
#endif
    volatile uint16_t gen;
    volatile uint16_t refcnt;
    volatile uint32_t len;
    uint8_t           buf[LP_BUF_SIZE];
#if CONFIG_CORE_LEASEPOOL_GUARD
    volatile uint32_t canary_tail;
#endif
} lp_slot_t;

static lp_slot_t          s_slots[LP_NUM_SLOTS] DMA_ATTR;
static uint16_t           s_free[LP_NUM_SLOTS];
static volatile uint16_t  s_free_top = 0;

static volatile uint32_t  s_alloc_ok = 0;
static volatile uint32_t  s_alloc_fail = 0;
static volatile uint16_t  s_peak_used = 0;
static volatile uint32_t  s_guard_failures = 0;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline bool lp_valid_idx_(uint16_t idx)
{
    return (idx < (uint16_t)LP_NUM_SLOTS);
}

#if CONFIG_CORE_LEASEPOOL_GUARD
static inline void lp_guard_fail_(const char* api, const char* why, lp_handle_t h)
{
    // Best-effort (debug): liczniki nie muszą być idealnie atomowe.
    s_guard_failures++;

    LP_DIAG_PRINTF("LP GUARD FAIL: %s: %s (idx=%u gen=%u)\n",
                   api ? api : "?",
                   why ? why : "?",
                   (unsigned)h.idx,
                   (unsigned)h.gen);
    abort();
}

static inline void lp_guard_check_canary_(const lp_slot_t* s, const char* api, lp_handle_t h)
{
    if (!s) lp_guard_fail_(api, "null slot", h);
    if (s->canary_head != LP_CANARY_VALUE) lp_guard_fail_(api, "canary_head corrupted", h);
    if (s->canary_tail != LP_CANARY_VALUE) lp_guard_fail_(api, "canary_tail corrupted", h);
}

static inline void lp_guard_check_magic_(const lp_slot_t* s, uint32_t expected, const char* api, lp_handle_t h)
{
    if (!s) lp_guard_fail_(api, "null slot", h);
    if (s->magic != expected) lp_guard_fail_(api, (expected == LP_MAGIC_FREE) ? "magic != FREE" : "magic != USED", h);
}

static inline void lp_guard_poison_fill_(uint8_t* p, uint32_t n, uint8_t v)
{
    if (!p || n == 0) return;
    memset(p, (int)v, (size_t)n);
}

static inline void lp_guard_poison_expect_(const lp_slot_t* s, uint8_t expected, const char* api, lp_handle_t h)
{
    // wykrywa UAF-writes: slot powinien być w całości wypełniony wzorcem.
    const uint8_t* p = (const uint8_t*)s->buf;
    for (uint32_t i = 0; i < (uint32_t)LP_BUF_SIZE; ++i) {
        if (p[i] != expected) {
            lp_guard_fail_(api, "poison mismatch (UAF write?)", h);
        }
    }
}

static inline void lp_guard_set_free_(lp_slot_t* s)
{
    s->canary_head = LP_CANARY_VALUE;
    s->canary_tail = LP_CANARY_VALUE;
    s->magic = LP_MAGIC_FREE;
    lp_guard_poison_fill_(s->buf, (uint32_t)LP_BUF_SIZE, (uint8_t)LP_POISON_FREE);
}

static inline void lp_guard_set_used_(lp_slot_t* s)
{
    s->canary_head = LP_CANARY_VALUE;
    s->canary_tail = LP_CANARY_VALUE;
    s->magic = LP_MAGIC_USED;
    lp_guard_poison_fill_(s->buf, (uint32_t)LP_BUF_SIZE, (uint8_t)LP_POISON_ALLOC);
}
#endif // CONFIG_CORE_LEASEPOOL_GUARD

void lp_init(void)
{
    portENTER_CRITICAL(&s_mux);

    s_free_top = (uint16_t)LP_NUM_SLOTS;
    for (uint16_t i = 0; i < (uint16_t)LP_NUM_SLOTS; ++i) {
        s_free[i] = i;

        s_slots[i].gen = 1;
        s_slots[i].refcnt = 0;
        s_slots[i].len = 0;

#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_set_free_(&s_slots[i]);
#endif
    }

    s_alloc_ok = 0;
    s_alloc_fail = 0;
    s_peak_used = 0;
    s_guard_failures = 0;

    portEXIT_CRITICAL(&s_mux);

#if CONFIG_CORE_LEASEPOOL_SELFTEST_ON_BOOT
    const int issues = lp_check(false);
    if (issues == 0) {
        LP_DIAG_PRINTF("LeasePool selftest: OK (slots=%u cap=%u)\n",
                       (unsigned)LP_NUM_SLOTS,
                       (unsigned)LP_BUF_SIZE);
    } else {
        LP_DIAG_PRINTF("LeasePool selftest: FAIL (issues=%d slots=%u cap=%u)\n",
                       issues,
                       (unsigned)LP_NUM_SLOTS,
                       (unsigned)LP_BUF_SIZE);
    }
#endif
}

lp_handle_t lp_alloc_try(uint32_t want_len)
{
    lp_handle_t h = lp_invalid_handle();

    if (want_len > (uint32_t)LP_BUF_SIZE) {
        return h;
    }

    portENTER_CRITICAL(&s_mux);

    if (s_free_top == 0) {
        s_alloc_fail++;
        portEXIT_CRITICAL(&s_mux);
        return h;
    }

    const uint16_t idx = s_free[--s_free_top];
    lp_slot_t* s = &s_slots[idx];

#if CONFIG_CORE_LEASEPOOL_GUARD
    // Slot właśnie zszedł z listy FREE: musi wyglądać jak FREE.
    h.idx = idx;
    h.gen = s->gen;
    lp_guard_check_canary_(s, "lp_alloc_try", h);
    lp_guard_check_magic_(s, LP_MAGIC_FREE, "lp_alloc_try", h);
    lp_guard_poison_expect_(s, (uint8_t)LP_POISON_FREE, "lp_alloc_try", h);

    lp_guard_set_used_(s);
#endif

    s->refcnt = 1;
    s->len = 0;

    // handle
    h.idx = idx;
    h.gen = s->gen;

    // statystyki
    s_alloc_ok++;
    const uint16_t used = (uint16_t)((uint16_t)LP_NUM_SLOTS - s_free_top);
    if (used > s_peak_used) s_peak_used = used;

    portEXIT_CRITICAL(&s_mux);
    return h;
}

bool lp_acquire(lp_handle_t h, lp_view_t* out)
{
    if (!out) return false;
    if (!lp_valid_idx_(h.idx)) return false;

    bool ok = false;

    portENTER_CRITICAL(&s_mux);
    lp_slot_t* s = &s_slots[h.idx];

    if (s->gen == h.gen && s->refcnt > 0) {
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_check_canary_(s, "lp_acquire", h);
        lp_guard_check_magic_(s, LP_MAGIC_USED, "lp_acquire", h);
#endif
        out->ptr = (void*)s->buf;
        out->len = s->len;
        out->cap = (uint32_t)LP_BUF_SIZE;
        ok = true;
    }

    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void lp_commit(lp_handle_t h, uint32_t len)
{
    if (!lp_valid_idx_(h.idx)) return;

    portENTER_CRITICAL(&s_mux);
    lp_slot_t* s = &s_slots[h.idx];

    if (s->gen != h.gen || s->refcnt == 0) {
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_commit", "invalid handle or refcnt==0", h);
#endif
        portEXIT_CRITICAL(&s_mux);
        return;
    }

#if CONFIG_CORE_LEASEPOOL_GUARD
    lp_guard_check_canary_(s, "lp_commit", h);
    lp_guard_check_magic_(s, LP_MAGIC_USED, "lp_commit", h);
#endif

    if (len > (uint32_t)LP_BUF_SIZE) {
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_commit", "len > cap", h);
#endif
        len = (uint32_t)LP_BUF_SIZE;
    }

    // bariera kompilatora przed publikacją len
    __asm__ __volatile__("" ::: "memory");
    s->len = len;
    __asm__ __volatile__("" ::: "memory");

    portEXIT_CRITICAL(&s_mux);
}

void lp_addref_n(lp_handle_t h, uint16_t n)
{
    if (n == 0) return;
    if (!lp_valid_idx_(h.idx)) return;

    portENTER_CRITICAL(&s_mux);
    lp_slot_t* s = &s_slots[h.idx];

    if (s->gen != h.gen || s->refcnt == 0) {
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_addref_n", "invalid handle or refcnt==0", h);
#endif
        portEXIT_CRITICAL(&s_mux);
        return;
    }

#if CONFIG_CORE_LEASEPOOL_GUARD
    lp_guard_check_canary_(s, "lp_addref_n", h);
    lp_guard_check_magic_(s, LP_MAGIC_USED, "lp_addref_n", h);
#endif

    if ((uint32_t)s->refcnt + (uint32_t)n > 0xFFFFu) {
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_addref_n", "refcnt overflow", h);
#endif
        s->refcnt = 0xFFFFu;
        portEXIT_CRITICAL(&s_mux);
        return;
    }

    s->refcnt = (uint16_t)(s->refcnt + n);
    portEXIT_CRITICAL(&s_mux);
}

void lp_release(lp_handle_t h)
{
    if (!lp_valid_idx_(h.idx)) return;

    portENTER_CRITICAL(&s_mux);
    lp_slot_t* s = &s_slots[h.idx];

    if (s->gen != h.gen) {
        // stale handle / UAF
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_release", "gen mismatch (stale handle)", h);
#endif
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    if (s->refcnt == 0) {
        // double free
#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_fail_("lp_release", "refcnt==0 (double free)", h);
#endif
        portEXIT_CRITICAL(&s_mux);
        return;
    }

#if CONFIG_CORE_LEASEPOOL_GUARD
    // wykrywa overflow jeszcze przed oddaniem slota do puli
    lp_guard_check_canary_(s, "lp_release", h);
    lp_guard_check_magic_(s, LP_MAGIC_USED, "lp_release", h);
#endif

    s->refcnt--;

    if (s->refcnt == 0) {
        s->len = 0;
        s->gen++;

#if CONFIG_CORE_LEASEPOOL_GUARD
        lp_guard_set_free_(s);
#endif

        if (s_free_top >= (uint16_t)LP_NUM_SLOTS) {
#if CONFIG_CORE_LEASEPOOL_GUARD
            lp_guard_fail_("lp_release", "free list overflow", h);
#endif
            // best-effort: nie zapisujemy poza tablicę
        } else {
            s_free[s_free_top++] = h.idx;
        }
    }

    portEXIT_CRITICAL(&s_mux);
}

uint16_t lp_free_count(void)
{
    return s_free_top;
}

uint16_t lp_used_count(void)
{
    return (uint16_t)((uint16_t)LP_NUM_SLOTS - s_free_top);
}

void lp_get_stats(lp_stats_t* out)
{
    if (!out) return;

    portENTER_CRITICAL(&s_mux);
    const uint16_t free_ = s_free_top;
    const uint16_t used_ = (uint16_t)((uint16_t)LP_NUM_SLOTS - free_);

    out->slots_total     = (uint16_t)LP_NUM_SLOTS;
    out->slots_free      = free_;
    out->slots_used      = used_;
    out->slots_peak_used = s_peak_used;
    out->alloc_ok        = s_alloc_ok;
    out->drops_alloc_fail= s_alloc_fail;
    out->guard_failures  = s_guard_failures;

    portEXIT_CRITICAL(&s_mux);
}

void lp_reset_stats(void)
{
    portENTER_CRITICAL(&s_mux);
    s_alloc_ok = 0;
    s_alloc_fail = 0;
    s_guard_failures = 0;

    const uint16_t used_ = (uint16_t)((uint16_t)LP_NUM_SLOTS - s_free_top);
    s_peak_used = used_;

    portEXIT_CRITICAL(&s_mux);
}

typedef struct {
#if CONFIG_CORE_LEASEPOOL_GUARD
    uint32_t canary_head;
    uint32_t canary_tail;
    uint32_t magic;
#endif
    uint16_t gen;
    uint16_t refcnt;
    uint32_t len;
} lp_slot_snap_t;

typedef struct {
    uint16_t free_top;
    uint16_t free_list[LP_NUM_SLOTS];
    lp_slot_snap_t slots[LP_NUM_SLOTS];
    lp_stats_t stats;
} lp_snapshot_t;

static void lp_snapshot_(lp_snapshot_t* snap)
{
    if (!snap) return;

    portENTER_CRITICAL(&s_mux);

    snap->free_top = s_free_top;
    for (uint16_t i = 0; i < (uint16_t)LP_NUM_SLOTS; ++i) {
        if (i < s_free_top) {
            snap->free_list[i] = s_free[i];
        } else {
            snap->free_list[i] = 0xFFFFu;
        }

        lp_slot_t* s = &s_slots[i];
        snap->slots[i].gen = s->gen;
        snap->slots[i].refcnt = s->refcnt;
        snap->slots[i].len = s->len;
#if CONFIG_CORE_LEASEPOOL_GUARD
        snap->slots[i].canary_head = s->canary_head;
        snap->slots[i].canary_tail = s->canary_tail;
        snap->slots[i].magic = s->magic;
#endif
    }

    snap->stats.slots_total = (uint16_t)LP_NUM_SLOTS;
    snap->stats.slots_free  = s_free_top;
    snap->stats.slots_used  = (uint16_t)((uint16_t)LP_NUM_SLOTS - s_free_top);
    snap->stats.slots_peak_used = s_peak_used;
    snap->stats.alloc_ok = s_alloc_ok;
    snap->stats.drops_alloc_fail = s_alloc_fail;
    snap->stats.guard_failures = s_guard_failures;

    portEXIT_CRITICAL(&s_mux);
}

static void lp_check_print_(bool verbose, int issues, const lp_stats_t* st)
{
    if (!verbose) return;

    if (issues == 0) {
        LP_DIAG_PRINTF("lp_check: OK (slots=%u used=%u free=%u peak=%u alloc_ok=%u alloc_fail=%u)\n",
                       (unsigned)st->slots_total,
                       (unsigned)st->slots_used,
                       (unsigned)st->slots_free,
                       (unsigned)st->slots_peak_used,
                       (unsigned)st->alloc_ok,
                       (unsigned)st->drops_alloc_fail);
    } else {
        LP_DIAG_PRINTF("lp_check: FAIL (issues=%d slots=%u used=%u free=%u peak=%u alloc_ok=%u alloc_fail=%u)\n",
                       issues,
                       (unsigned)st->slots_total,
                       (unsigned)st->slots_used,
                       (unsigned)st->slots_free,
                       (unsigned)st->slots_peak_used,
                       (unsigned)st->alloc_ok,
                       (unsigned)st->drops_alloc_fail);
    }
}

int lp_check(bool verbose)
{
    lp_snapshot_t snap;
    lp_snapshot_(&snap);

    int issues = 0;

    // 1) sanity free_top
    if (snap.free_top > (uint16_t)LP_NUM_SLOTS) {
        if (verbose) {
            LP_DIAG_PRINTF("FAIL: free_top out of range: %u (max=%u)\n",
                           (unsigned)snap.free_top,
                           (unsigned)LP_NUM_SLOTS);
        }
        issues++;
        snap.free_top = (uint16_t)LP_NUM_SLOTS;
    }

    // 2) free list: zakres + duplikaty
    bool in_free[LP_NUM_SLOTS];
    for (uint16_t i = 0; i < (uint16_t)LP_NUM_SLOTS; ++i) in_free[i] = false;

    for (uint16_t i = 0; i < snap.free_top; ++i) {
        const uint16_t idx = snap.free_list[i];
        if (!lp_valid_idx_(idx)) {
            if (verbose) {
                LP_DIAG_PRINTF("FAIL: free_list[%u] invalid idx=%u\n",
                               (unsigned)i,
                               (unsigned)idx);
            }
            issues++;
            continue;
        }
        if (in_free[idx]) {
            if (verbose) {
                LP_DIAG_PRINTF("FAIL: free_list duplicate idx=%u\n", (unsigned)idx);
            }
            issues++;
        }
        in_free[idx] = true;
    }

    // 3) per-slot invariants
    for (uint16_t i = 0; i < (uint16_t)LP_NUM_SLOTS; ++i) {
        const lp_slot_snap_t* s = &snap.slots[i];

#if CONFIG_CORE_LEASEPOOL_GUARD
        if (s->canary_head != LP_CANARY_VALUE || s->canary_tail != LP_CANARY_VALUE) {
            if (verbose) {
                LP_DIAG_PRINTF("FAIL: canary corrupted idx=%u head=0x%08X tail=0x%08X\n",
                               (unsigned)i,
                               (unsigned)s->canary_head,
                               (unsigned)s->canary_tail);
            }
            issues++;
        }
#endif

        if (in_free[i]) {
            // FREE slot: refcnt==0, len==0
            if (s->refcnt != 0) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: FREE slot has refcnt!=0 idx=%u ref=%u\n",
                                   (unsigned)i,
                                   (unsigned)s->refcnt);
                }
                issues++;
            }
            if (s->len != 0) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: FREE slot has len!=0 idx=%u len=%u\n",
                                   (unsigned)i,
                                   (unsigned)s->len);
                }
                issues++;
            }
#if CONFIG_CORE_LEASEPOOL_GUARD
            if (s->magic != LP_MAGIC_FREE) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: FREE slot magic mismatch idx=%u magic=0x%08X\n",
                                   (unsigned)i,
                                   (unsigned)s->magic);
                }
                issues++;
            }
#endif
        } else {
            // USED slot: refcnt>0, len<=cap
            if (s->refcnt == 0) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: USED slot has refcnt==0 idx=%u\n", (unsigned)i);
                }
                issues++;
            }
            if (s->len > (uint32_t)LP_BUF_SIZE) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: USED slot len>cap idx=%u len=%u cap=%u\n",
                                   (unsigned)i,
                                   (unsigned)s->len,
                                   (unsigned)LP_BUF_SIZE);
                }
                issues++;
            }
#if CONFIG_CORE_LEASEPOOL_GUARD
            if (s->magic != LP_MAGIC_USED) {
                if (verbose) {
                    LP_DIAG_PRINTF("FAIL: USED slot magic mismatch idx=%u magic=0x%08X\n",
                                   (unsigned)i,
                                   (unsigned)s->magic);
                }
                issues++;
            }
#endif
        }
    }

    lp_check_print_(verbose, issues, &snap.stats);
    return issues;
}

void lp_dump(void)
{
    lp_snapshot_t snap;
    lp_snapshot_(&snap);

    LP_DIAG_PRINTF("leasepool: slots=%u cap=%u used=%u free=%u peak=%u alloc_ok=%u alloc_fail=%u guard_fail=%u\n",
                   (unsigned)snap.stats.slots_total,
                   (unsigned)LP_BUF_SIZE,
                   (unsigned)snap.stats.slots_used,
                   (unsigned)snap.stats.slots_free,
                   (unsigned)snap.stats.slots_peak_used,
                   (unsigned)snap.stats.alloc_ok,
                   (unsigned)snap.stats.drops_alloc_fail,
                   (unsigned)snap.stats.guard_failures);

#if CONFIG_CORE_LEASEPOOL_GUARD
    LP_DIAG_PRINTF("idx gen  ref  len   magic      canary\n");
    LP_DIAG_PRINTF("--- ---- ---- ----- ---------- ----------\n");
#else
    LP_DIAG_PRINTF("idx gen  ref  len\n");
    LP_DIAG_PRINTF("--- ---- ---- -----\n");
#endif

    for (uint16_t i = 0; i < (uint16_t)LP_NUM_SLOTS; ++i) {
        const lp_slot_snap_t* s = &snap.slots[i];
#if CONFIG_CORE_LEASEPOOL_GUARD
        LP_DIAG_PRINTF("%3u %4u %4u %5u 0x%08X 0x%08X\n",
                       (unsigned)i,
                       (unsigned)s->gen,
                       (unsigned)s->refcnt,
                       (unsigned)s->len,
                       (unsigned)s->magic,
                       (unsigned)s->canary_head);
#else
        LP_DIAG_PRINTF("%3u %4u %4u %5u\n",
                       (unsigned)i,
                       (unsigned)s->gen,
                       (unsigned)s->refcnt,
                       (unsigned)s->len);
#endif
    }

    LP_DIAG_PRINTF("free_top=%u\nfree_list:", (unsigned)snap.free_top);
    for (uint16_t i = 0; i < snap.free_top; ++i) {
        LP_DIAG_PRINTF(" %u", (unsigned)snap.free_list[i]);
    }
    LP_DIAG_PRINTF("\n");
}
