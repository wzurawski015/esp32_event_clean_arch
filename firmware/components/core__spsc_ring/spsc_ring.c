#include "core/spsc_ring.h"

static inline bool is_pow2_u32_(const uint32_t x)
{
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

static inline uint32_t load_acquire_u32_(const uint32_t* p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline uint32_t load_relaxed_u32_(const uint32_t* p)
{
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}

static inline void store_release_u32_(uint32_t* p, const uint32_t v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

bool spsc_ring_init(spsc_ring_t* rb, void* storage, const uint32_t cap_bytes)
{
    if (!rb || !storage)
    {
        return false;
    }

    // Wymagamy potęgi 2 (przyspiesza wrap; eliminuje koszt modulo).
    if (!is_pow2_u32_(cap_bytes) || cap_bytes < 2u)
    {
        return false;
    }

    rb->buf  = (uint8_t*)storage;
    rb->cap  = cap_bytes;
    rb->mask = cap_bytes - 1u;

    // ring jest czysto bajtowy; nie inicjalizujemy bufora (caller może chcieć zachować śmieci dla debug).
    __atomic_store_n(&rb->head, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&rb->tail, 0u, __ATOMIC_RELAXED);
    return true;
}

size_t spsc_ring_used(const spsc_ring_t* rb)
{
    if (!rb)
    {
        return 0u;
    }
    const uint32_t head = load_acquire_u32_(&rb->head);
    const uint32_t tail = load_acquire_u32_(&rb->tail);
    return (size_t)(head - tail);
}

size_t spsc_ring_free(const spsc_ring_t* rb)
{
    if (!rb)
    {
        return 0u;
    }
    const uint32_t head = load_acquire_u32_(&rb->head);
    const uint32_t tail = load_acquire_u32_(&rb->tail);
    const uint32_t used = head - tail;
    return (used >= rb->cap) ? 0u : (size_t)(rb->cap - used);
}

uint8_t* spsc_ring_reserve(spsc_ring_t* rb, const size_t want, size_t* out_n)
{
    if (!rb || !out_n)
    {
        return NULL;
    }

    const uint32_t head = load_relaxed_u32_(&rb->head);              // producer‑only writer
    const uint32_t tail = load_acquire_u32_(&rb->tail);              // consumer publishes tail
    const uint32_t used = head - tail;

    if (used >= rb->cap)
    {
        *out_n = 0u;
        return NULL;
    }

    const uint32_t free_total = rb->cap - used;
    size_t n                 = want;
    if (n > (size_t)free_total)
    {
        n = (size_t)free_total;
    }

    const uint32_t off   = head & rb->mask;
    const uint32_t contig = rb->cap - off;
    if (n > (size_t)contig)
    {
        n = (size_t)contig;
    }

    *out_n = n;
    return (n == 0u) ? NULL : (rb->buf + off);
}

void spsc_ring_commit(spsc_ring_t* rb, const size_t n)
{
    if (!rb || n == 0u)
    {
        return;
    }

    const uint32_t head = load_relaxed_u32_(&rb->head);
    const uint32_t next = head + (uint32_t)n;
    store_release_u32_(&rb->head, next);
}

const uint8_t* spsc_ring_peek(const spsc_ring_t* rb, size_t* out_n)
{
    if (!rb || !out_n)
    {
        return NULL;
    }

    const uint32_t tail = load_relaxed_u32_(&rb->tail);              // consumer‑only writer
    const uint32_t head = load_acquire_u32_(&rb->head);              // producer publishes head
    const uint32_t used = head - tail;

    if (used == 0u)
    {
        *out_n = 0u;
        return NULL;
    }

    const uint32_t off    = tail & rb->mask;
    const uint32_t contig = rb->cap - off;
    size_t n              = (size_t)used;
    if (n > (size_t)contig)
    {
        n = (size_t)contig;
    }

    *out_n = n;
    return (n == 0u) ? NULL : (rb->buf + off);
}

void spsc_ring_consume(spsc_ring_t* rb, const size_t n)
{
    if (!rb || n == 0u)
    {
        return;
    }

    const uint32_t tail = load_relaxed_u32_(&rb->tail);
    const uint32_t next = tail + (uint32_t)n;
    store_release_u32_(&rb->tail, next);
}
