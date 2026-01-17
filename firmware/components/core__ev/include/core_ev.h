#pragma once

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "core/leasepool.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EV_MAX_SUBS
#  ifdef CONFIG_CORE_EV_MAX_SUBS
#    define EV_MAX_SUBS CONFIG_CORE_EV_MAX_SUBS
#  else
#    define EV_MAX_SUBS 12
#  endif
#endif

typedef uint16_t ev_src_t;

enum {
    EV_SRC_SYS   = 0x01,
    EV_SRC_TIMER = 0x02,
    EV_SRC_I2C   = 0x03,
    EV_SRC_LCD   = 0x04,
    EV_SRC_DS18  = 0x05,
    EV_SRC_LOG   = 0x06,
    EV_SRC_UART  = 0x07,
    EV_SRC_GPIO  = 0x08,
};

typedef enum {
    EVK_NONE = 0,
    EVK_COPY,
    EVK_LEASE,
    EVK_STREAM,
} ev_kind_t;

typedef enum {
    EVQ_DROP_NEW = 0,
    EVQ_REPLACE_LAST,
} ev_qos_t;

/* Flagi metadanych zdarzeń */
enum {
    EVF_NONE     = 0u,
    EVF_CRITICAL = (1u << 0),
    EVF_ALL      = EVF_CRITICAL,
};

#include "core_ev_schema.h"

enum {
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) NAME = (CODE),
    EV_SCHEMA(X)
#undef X
};

typedef struct {
    ev_src_t    src;
    uint16_t    code;
    ev_kind_t   kind;
    ev_qos_t    qos;
    uint16_t    flags;
    const char* name;
    const char* doc;
} ev_meta_t;

const ev_meta_t* ev_meta_find(ev_src_t src, uint16_t code);
const char* ev_code_name(ev_src_t src, uint16_t code);
const char* ev_kind_str(ev_kind_t kind);
const char* ev_qos_str(ev_qos_t qos);

size_t ev_meta_count(void);
const ev_meta_t* ev_meta_by_index(size_t idx);

/* Statystyki per-event */
typedef struct {
    uint32_t posts_ok;
    uint32_t posts_drop;
    uint32_t enq_fail;
    uint32_t delivered;
} ev_event_stats_t;

size_t ev_get_event_stats(ev_event_stats_t* out, size_t max);

typedef struct {
    ev_src_t  src;
    uint16_t  code;
    uint32_t  a0;
    uint32_t  a1;
    uint32_t  t_ms;
} ev_msg_t;

typedef QueueHandle_t ev_queue_t;

void ev_init(void);
bool ev_subscribe(ev_queue_t* out_q, size_t depth);
bool ev_unsubscribe(ev_queue_t q);
bool ev_post(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);
bool ev_post_lease(ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len);
bool ev_post_from_isr(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);

/* =========================
 * PR7: EventBus jako port (vtbl) — dependency injection
 * ========================= */

typedef struct ev_bus_vtbl {
    bool (*post)(void* self, ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);
    bool (*post_lease)(void* self, ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len);
    bool (*post_from_isr)(void* self, ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1);
    bool (*subscribe)(void* self, ev_queue_t* out_q, size_t depth);
    bool (*unsubscribe)(void* self, ev_queue_t q);
} ev_bus_vtbl_t;

typedef struct ev_bus {
    void* self;
    const ev_bus_vtbl_t* vtbl;
} ev_bus_t;

const ev_bus_t* ev_bus_default(void);

static inline bool ev_bus_post(const ev_bus_t* bus, ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
    return (bus && bus->vtbl && bus->vtbl->post) ? bus->vtbl->post(bus->self, src, code, a0, a1) : false;
}

static inline bool ev_bus_post_lease(const ev_bus_t* bus, ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len)
{
    return (bus && bus->vtbl && bus->vtbl->post_lease) ? bus->vtbl->post_lease(bus->self, src, code, h, len) : false;
}

static inline bool ev_bus_post_from_isr(const ev_bus_t* bus, ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
    return (bus && bus->vtbl && bus->vtbl->post_from_isr) ? bus->vtbl->post_from_isr(bus->self, src, code, a0, a1) : false;
}

static inline bool ev_bus_subscribe(const ev_bus_t* bus, ev_queue_t* out_q, size_t depth)
{
    return (bus && bus->vtbl && bus->vtbl->subscribe) ? bus->vtbl->subscribe(bus->self, out_q, depth) : false;
}

static inline bool ev_bus_unsubscribe(const ev_bus_t* bus, ev_queue_t q)
{
    return (bus && bus->vtbl && bus->vtbl->unsubscribe) ? bus->vtbl->unsubscribe(bus->self, q) : false;
}

/* Statystyki globalne busa */
typedef struct {
    uint16_t subs_active;
    uint16_t subs_max;
    uint32_t posts_ok;
    uint32_t posts_drop;
    uint32_t enq_fail;
    uint16_t q_depth_max;
} ev_stats_t;

void ev_get_stats(ev_stats_t* out);
void ev_reset_stats(void);

#ifdef __cplusplus
}
#endif
