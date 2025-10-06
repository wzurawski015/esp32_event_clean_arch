#include "core_ev.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

/* Jedno źródło prawdy dla EV_MAX_SUBS jest w core_ev.h — tutaj bez fallbacków. */
#if (EV_MAX_SUBS < 1)
#  error "EV_MAX_SUBS must be >= 1"
#endif

/** Wewnętrzny opis subskrybenta. */
typedef struct {
    ev_queue_t q;
    uint16_t   depth;
} ev_sub_t;

/* ====== Stan globalny busa ====== */
static ev_sub_t  s_subs[EV_MAX_SUBS];
static uint16_t  s_subs_cnt;
static uint16_t  s_q_depth_max;

static uint32_t  s_posts_ok;
static uint32_t  s_posts_drop;
static uint32_t  s_enq_fail;

/* Sekcje krytyczne: w portach IDF z portMUX używamy *_ISR w ISR. */
#if defined(portMUX_INITIALIZER_UNLOCKED)
static portMUX_TYPE s_ev_mux = portMUX_INITIALIZER_UNLOCKED;
#  define EV_CS_ENTER()      portENTER_CRITICAL(&s_ev_mux)
#  define EV_CS_EXIT()       portEXIT_CRITICAL(&s_ev_mux)
#  define EV_CS_ENTER_ISR()  portENTER_CRITICAL_ISR(&s_ev_mux)
#  define EV_CS_EXIT_ISR()   portEXIT_CRITICAL_ISR(&s_ev_mux)
#else
#  define EV_CS_ENTER()      taskENTER_CRITICAL()
#  define EV_CS_EXIT()       taskEXIT_CRITICAL()
/* Na platformach bez portMUX używamy tych samych makr także w ISR. */
#  define EV_CS_ENTER_ISR()  taskENTER_CRITICAL()
#  define EV_CS_EXIT_ISR()   taskEXIT_CRITICAL()
#endif

static inline uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Wysyła ramkę do wszystkich subów; zwraca liczbę rzeczywistych dostarczeń. */
static uint16_t ev_broadcast(const ev_msg_t* m)
{
    uint16_t delivered = 0;

    /* Snapshot listy subskrybentów (krótka sekcja krytyczna). */
    EV_CS_ENTER();
    uint16_t n = s_subs_cnt;
    ev_sub_t local[EV_MAX_SUBS];
    if (n > EV_MAX_SUBS) n = EV_MAX_SUBS;
    memcpy(local, s_subs, n * sizeof(ev_sub_t));
    EV_CS_EXIT();

    for (uint16_t i = 0; i < n; ++i) {
        if (local[i].q == NULL) continue;
        BaseType_t ok = xQueueSend(local[i].q, m, 0); // NIE blokujemy producenta
        if (ok == pdTRUE) {
            delivered++;
        } else {
            /* Kolejka subskrybenta pełna. */
            EV_CS_ENTER();
            s_enq_fail++;
            EV_CS_EXIT();
        }
    }
    return delivered;
}

/* ====== API ====== */

void ev_init(void)
{
    EV_CS_ENTER();
    memset(s_subs, 0, sizeof(s_subs));
    s_subs_cnt    = 0;
    s_q_depth_max = 0;
    s_posts_ok    = 0;
    s_posts_drop  = 0;
    s_enq_fail    = 0;
    EV_CS_EXIT();
}

bool ev_subscribe(ev_queue_t* out_q, size_t depth)
{
    if (!out_q) return false;
    if (depth == 0) depth = 8;

    ev_queue_t q = xQueueCreate((UBaseType_t)depth, sizeof(ev_msg_t));
    if (q == NULL) return false;

    bool attached = false;
    EV_CS_ENTER();
    if (s_subs_cnt < EV_MAX_SUBS) {
        s_subs[s_subs_cnt].q     = q;
        s_subs[s_subs_cnt].depth = (uint16_t)depth;
        s_subs_cnt++;
        if (depth > s_q_depth_max) s_q_depth_max = (uint16_t)depth;
        attached = true;
    }
    EV_CS_EXIT();

    if (!attached) {
        vQueueDelete(q);
        return false;
    }
    *out_q = q;
    return true;
}

bool ev_unsubscribe(ev_queue_t q)
{
    if (!q) return false;

    bool found = false;
    EV_CS_ENTER();
    for (uint16_t i = 0; i < s_subs_cnt; ++i) {
        if (s_subs[i].q == q) {
            s_subs[i].q = NULL;  // zostaw slot pusty (bez kompaktowania)
            found = true;
            break;
        }
    }
    EV_CS_EXIT();

    /* Nie wywołujemy tutaj vQueueDelete(q) – patrz komentarz w core_ev.h.
     * Właściciel aktora powinien zniszczyć kolejkę dopiero po quiesce. */
    return found;
}

bool ev_post(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
    ev_msg_t m = {
        .src  = src,
        .code = code,
        .a0   = a0,
        .a1   = a1,
        .t_ms = now_ms(),
    };
    uint16_t n = ev_broadcast(&m);

    EV_CS_ENTER();
    if (n > 0) s_posts_ok++; else s_posts_drop++;
    EV_CS_EXIT();

    return (n > 0);
}

bool ev_post_lease(ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len)
{
    /* Spakuj uchwyt do pól a0/a1. */
    ev_msg_t m = {
        .src  = src,
        .code = code,
        .a0   = lp_pack_handle_u32(h),
        .a1   = (uint32_t)len,
        .t_ms = now_ms(),
    };

    uint16_t delivered = ev_broadcast(&m);

    /* Podbij refcount o realny fan‑out... */
    if (delivered > 0) {
        lp_addref_n(h, delivered);
    }
    /* ...a następnie ZAWSZE zwolnij referencję producenta. */
    lp_release(h);

    EV_CS_ENTER();
    if (delivered > 0) s_posts_ok++; else s_posts_drop++;
    EV_CS_EXIT();

    return (delivered > 0);
}

bool ev_post_from_isr(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
    ev_msg_t m = {
        .src  = src,
        .code = code,
        .a0   = a0,
        .a1   = a1,
        .t_ms = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS),
    };

    /* Snapshot subskrybentów (wariant ISR‑safe). */
    EV_CS_ENTER_ISR();
    uint16_t n = s_subs_cnt;
    ev_sub_t local[EV_MAX_SUBS];
    if (n > EV_MAX_SUBS) n = EV_MAX_SUBS;
    memcpy(local, s_subs, n * sizeof(ev_sub_t));
    EV_CS_EXIT_ISR();

    uint16_t delivered = 0;
    BaseType_t hpw = pdFALSE;

    for (uint16_t i = 0; i < n; ++i) {
        if (local[i].q == NULL) continue;
        if (xQueueSendFromISR(local[i].q, &m, &hpw) == pdTRUE) {
            delivered++;
        } else {
            EV_CS_ENTER_ISR();
            s_enq_fail++;
            EV_CS_EXIT_ISR();
        }
    }

    EV_CS_ENTER_ISR();
    if (delivered > 0) s_posts_ok++; else s_posts_drop++;
    EV_CS_EXIT_ISR();

    if (hpw == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return (delivered > 0);
}

void ev_get_stats(ev_stats_t* out)
{
    if (!out) return;

    EV_CS_ENTER();
    /* Policz tylko faktycznych subskrybentów (q != NULL). */
    uint16_t subs = 0;
    for (uint16_t i = 0; i < s_subs_cnt; ++i) {
        if (s_subs[i].q) subs++;
    }
    out->subs        = subs;
    out->q_depth_max = s_q_depth_max;
    out->posts_ok    = s_posts_ok;
    out->posts_drop  = s_posts_drop;
    out->enq_fail    = s_enq_fail;
    EV_CS_EXIT();
}

void ev_reset_stats(void)
{
    EV_CS_ENTER();
    s_posts_ok   = 0;
    s_posts_drop = 0;
    s_enq_fail   = 0;
    EV_CS_EXIT();
}
