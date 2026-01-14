#include "core_ev.h"
#include <string.h>

#if (defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD) || \
    (defined(CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT) && CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT)
#include <stdio.h>
#include <stdlib.h>
#if defined(ESP_PLATFORM)
#include "esp_rom_sys.h"
#endif
#endif

#if (defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD) || \
    (defined(CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT) && CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT)
#  if defined(ESP_PLATFORM)
#    define EV_DIAG_PRINTF(...) esp_rom_printf(__VA_ARGS__)
#  else
#    define EV_DIAG_PRINTF(...) printf(__VA_ARGS__)
#  endif
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#if (EV_MAX_SUBS < 1)
#  error "EV_MAX_SUBS must be >= 1"
#endif

typedef struct {
    ev_queue_t q;
    uint16_t   depth;
} ev_sub_t;

static ev_sub_t  s_subs[EV_MAX_SUBS];
static uint16_t  s_subs_cnt;
static uint16_t  s_q_depth_max;

static uint32_t  s_posts_ok;
static uint32_t  s_posts_drop;
static uint32_t  s_enq_fail;

#if defined(portMUX_INITIALIZER_UNLOCKED)
static portMUX_TYPE s_ev_mux = portMUX_INITIALIZER_UNLOCKED;
#  define EV_CS_ENTER()      portENTER_CRITICAL(&s_ev_mux)
#  define EV_CS_EXIT()       portEXIT_CRITICAL(&s_ev_mux)
#  define EV_CS_ENTER_ISR()  portENTER_CRITICAL_ISR(&s_ev_mux)
#  define EV_CS_EXIT_ISR()   portEXIT_CRITICAL_ISR(&s_ev_mux)
#else
#  define EV_CS_ENTER()      taskENTER_CRITICAL()
#  define EV_CS_EXIT()       taskEXIT_CRITICAL()
#  define EV_CS_ENTER_ISR()  taskENTER_CRITICAL()
#  define EV_CS_EXIT_ISR()   taskEXIT_CRITICAL()
#endif

static inline uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

#if (defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD) || \
    (defined(CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT) && CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT)
static const char* ev_kind_str_(ev_kind_t k)
{
    switch (k) {
        case EVK_NONE:   return "NONE";
        case EVK_COPY:   return "COPY";
        case EVK_LEASE:  return "LEASE";
        case EVK_STREAM: return "STREAM";
        default:         return "?";
    }
}
static const char* ev_qos_str_(ev_qos_t qos)
{
    switch (qos) {
        case EVQ_DROP_NEW:     return "DROP_NEW";
        case EVQ_REPLACE_LAST: return "REPLACE_LAST";
        default:               return "UNKNOWN";
    }
}
#endif

/* ===================== SCHEMA ===================== */

static const ev_meta_t s_ev_meta[] = {
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) \
    { .src = (SRC), .code = (CODE), .kind = EVK_##KIND, .qos = EVQ_##QOS, .flags = (uint16_t)(FLAGS), .name = #NAME, .doc = (DOC) },
    EV_SCHEMA(X)
#undef X
};

enum { EV_META_LEN = (int)(sizeof(s_ev_meta) / sizeof(s_ev_meta[0])) };

static const size_t s_ev_meta_len = (size_t)EV_META_LEN;

/* FIX: Poprawione nazwy tablic statystyk (usunięto 't' z s_evt_) */
static uint32_t s_ev_posts_ok[EV_META_LEN];
static uint32_t s_ev_posts_drop[EV_META_LEN];
static uint32_t s_ev_enq_fail[EV_META_LEN];
static uint32_t s_ev_delivered[EV_META_LEN];

const ev_meta_t* ev_meta_find(ev_src_t src, uint16_t code)
{
    for (size_t i = 0; i < s_ev_meta_len; ++i) {
        if (s_ev_meta[i].src == src && s_ev_meta[i].code == code) {
            return &s_ev_meta[i];
        }
    }
    return NULL;
}

const char* ev_code_name(ev_src_t src, uint16_t code)
{
    const ev_meta_t* m = ev_meta_find(src, code);
    return m ? m->name : "EV_UNKNOWN";
}

/* ===================== GUARDS ===================== */

#if defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD

static void ev_schema_abort_(const char* api,
                             ev_src_t src,
                             uint16_t code,
                             const ev_meta_t* meta,
                             const char* why)
{
    if (meta) {
        EV_DIAG_PRINTF(
            "EV SCHEMA VIOLATION: %s: %s (src=0x%04X code=0x%04X name=%s kind=%s qos=%s flags=0x%04X)\n",
               api,
               why,
               (unsigned)src,
               (unsigned)code,
               meta->name ? meta->name : "?",
               ev_kind_str_(meta->kind),
               ev_qos_str_(meta->qos),
               (unsigned)meta->flags);
    } else {
        EV_DIAG_PRINTF("EV SCHEMA VIOLATION: %s: %s (src=0x%04X code=0x%04X name=EV_UNKNOWN)\n",
               api,
               why,
               (unsigned)src,
               (unsigned)code);
    }
    abort();
}

static const ev_meta_t* ev_schema_require_known_(const char* api, ev_src_t src, uint16_t code)
{
    const ev_meta_t* meta = ev_meta_find(src, code);
    if (!meta) {
        ev_schema_abort_(api, src, code, NULL, "event not present in schema");
    }
    return meta;
}

static void ev_schema_require_kind_1_(const char* api, ev_src_t src, uint16_t code, const ev_meta_t* meta, ev_kind_t k0)
{
    if (!meta) ev_schema_abort_(api, src, code, NULL, "internal: meta==NULL");
    if (meta->kind != k0) ev_schema_abort_(api, src, code, meta, "wrong API for event kind");
}

static void ev_schema_require_kind_2_(const char* api, ev_src_t src, uint16_t code, const ev_meta_t* meta, ev_kind_t k0, ev_kind_t k1)
{
    if (!meta) ev_schema_abort_(api, src, code, NULL, "internal: meta==NULL");
    if (!(meta->kind == k0 || meta->kind == k1)) ev_schema_abort_(api, src, code, meta, "wrong API for event kind");
}

static void ev_schema_require_none_payload_(const char* api, ev_src_t src, uint16_t code, const ev_meta_t* meta, uint32_t a0, uint32_t a1)
{
    if (!meta) ev_schema_abort_(api, src, code, NULL, "internal: meta==NULL");
    if (meta->kind == EVK_NONE && (a0 != 0u || a1 != 0u)) {
        ev_schema_abort_(api, src, code, meta, "EVK_NONE requires a0=a1=0");
    }
}

#endif // GUARD

/* ===================== SELFTEST ===================== */

#if defined(CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT) && CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT

static bool ev_schema_is_critical_(const ev_meta_t* m)
{
    return (m && ((m->flags & EVF_CRITICAL) != 0));
}

static void ev_schema_selftest_or_abort_(void)
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    int issues = 0;

    // 1) duplikaty (src,code)
    for (size_t i = 0; i < s_ev_meta_len; ++i) {
        for (size_t j = i + 1; j < s_ev_meta_len; ++j) {
            if (s_ev_meta[i].src == s_ev_meta[j].src && s_ev_meta[i].code == s_ev_meta[j].code) {
                EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: dup src+code: src=0x%04X code=0x%04X\n",
                               (unsigned)s_ev_meta[i].src, (unsigned)s_ev_meta[i].code);
                issues++;
            }
        }
    }

    // 2) duplikaty name
    for (size_t i = 0; i < s_ev_meta_len; ++i) {
        if (!s_ev_meta[i].name || s_ev_meta[i].name[0] == '\0') continue;
        for (size_t j = i + 1; j < s_ev_meta_len; ++j) {
            if (!s_ev_meta[j].name || s_ev_meta[j].name[0] == '\0') continue;
            if (strcmp(s_ev_meta[i].name, s_ev_meta[j].name) == 0) {
                EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: dup name: %s\n", s_ev_meta[i].name);
                issues++;
            }
        }
    }

    // 3) sanity
    for (size_t i = 0; i < s_ev_meta_len; ++i) {
        const ev_meta_t* m = &s_ev_meta[i];

        if (!m->name || m->name[0] == '\0') {
            EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: empty name idx=%u\n", (unsigned)i);
            issues++;
        }
        if ((unsigned)m->kind > (unsigned)EVK_STREAM) {
            EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: invalid kind idx=%u\n", (unsigned)i);
            issues++;
        }
        if ((unsigned)m->qos > (unsigned)EVQ_REPLACE_LAST) {
            EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: invalid qos idx=%u\n", (unsigned)i);
            issues++;
        }
        if (m->qos == EVQ_REPLACE_LAST && (m->kind != EVK_NONE && m->kind != EVK_COPY)) {
            EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: qos=REPLACE_LAST invalid for kind idx=%u\n", (unsigned)i);
            issues++;
        }
        
        if ((m->flags & (uint16_t)~EVF_ALL) != 0) {
             EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: unknown flags idx=%u flags=0x%04X\n", (unsigned)i, (unsigned)m->flags);
             issues++;
        }

        if (ev_schema_is_critical_(m)) {
            if (!m->doc || m->doc[0] == '\0') {
                EV_DIAG_PRINTF("EV SCHEMA SELFTEST FAIL: missing doc (CRITICAL) idx=%u\n", (unsigned)i);
                issues++;
            }
        }
    }

    if (issues == 0) {
        EV_DIAG_PRINTF("EV schema selftest: OK (entries=%u)\n", (unsigned)s_ev_meta_len);
        return;
    }
    EV_DIAG_PRINTF("EV schema selftest: FAIL (issues=%d)\n", issues);
    abort();
}

#endif // SELFTEST

/* ====== BUS LOGIC ====== */

typedef struct {
    uint16_t delivered;
    uint16_t enq_fail;
} ev_fanout_t;

static ev_fanout_t ev_broadcast(const ev_msg_t* m, ev_qos_t qos)
{
    ev_fanout_t r = {0};

    EV_CS_ENTER();
    uint16_t n = s_subs_cnt;
    ev_sub_t local[EV_MAX_SUBS];
    if (n > EV_MAX_SUBS) n = EV_MAX_SUBS;
    memcpy(local, s_subs, n * sizeof(ev_sub_t));
    EV_CS_EXIT();

    for (uint16_t i = 0; i < n; ++i) {
        if (local[i].q == NULL) continue;

        BaseType_t ok = pdFALSE;
        if (qos == EVQ_REPLACE_LAST && local[i].depth == 1) {
            ok = xQueueOverwrite(local[i].q, m);
        } else {
            ok = xQueueSend(local[i].q, m, 0);
        }

        if (ok == pdTRUE) r.delivered++;
        else              r.enq_fail++;
    }
    return r;
}

static ev_fanout_t ev_broadcast_lease(const ev_msg_t* m, lp_handle_t h)
{
    ev_fanout_t r = {0};
    ev_sub_t local[EV_MAX_SUBS] = { 0 };
    uint16_t n = 0;

    EV_CS_ENTER();
    n = s_subs_cnt;
    for (uint16_t i = 0; i < n && i < EV_MAX_SUBS; ++i) local[i] = s_subs[i];
    EV_CS_EXIT();

    for (uint16_t i = 0; i < n && i < EV_MAX_SUBS; ++i) {
        if (local[i].q == NULL) continue;
        lp_addref_n(h, 1);
        if (xQueueSend(local[i].q, m, 0) == pdTRUE) {
            r.delivered++;
            continue;
        }
        r.enq_fail++;
        lp_release(h);
    }
    return r;
}

/* ====== PUBLIC API ====== */

void ev_init(void)
{
    EV_CS_ENTER();
    memset(s_subs, 0, sizeof(s_subs));
    s_subs_cnt    = 0;
    s_q_depth_max = 0;
    s_posts_ok    = 0;
    s_posts_drop  = 0;
    s_enq_fail    = 0;
    
    memset(s_ev_posts_ok,   0, sizeof(s_ev_posts_ok));
    memset(s_ev_posts_drop, 0, sizeof(s_ev_posts_drop));
    memset(s_ev_enq_fail,   0, sizeof(s_ev_enq_fail));
    memset(s_ev_delivered,  0, sizeof(s_ev_delivered));
    EV_CS_EXIT();

#if defined(CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT) && CONFIG_CORE_EV_SCHEMA_SELFTEST_ON_BOOT
    ev_schema_selftest_or_abort_();
#endif
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
    if (!attached) { vQueueDelete(q); return false; }
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
            s_subs[i].q = NULL;
            found = true;
            break;
        }
    }
    EV_CS_EXIT();
    return found;
}

bool ev_post(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
    const ev_meta_t* meta = NULL;
#if defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD
    meta = ev_schema_require_known_("ev_post", src, code);
    ev_schema_require_kind_2_("ev_post", src, code, meta, EVK_NONE, EVK_COPY);
    ev_schema_require_none_payload_("ev_post", src, code, meta, a0, a1);
#else
    meta = ev_meta_find(src, code);
#endif

    const size_t idx = (meta ? (size_t)(meta - s_ev_meta) : (size_t)-1);
    const ev_qos_t qos = (meta ? meta->qos : EVQ_DROP_NEW);

    ev_msg_t m = { .src=src, .code=code, .a0=a0, .a1=a1, .t_ms=now_ms() };
    const ev_fanout_t fo = ev_broadcast(&m, qos);

    EV_CS_ENTER();
    if (fo.enq_fail) {
        s_enq_fail += fo.enq_fail;
        if (idx != (size_t)-1) s_ev_enq_fail[idx] += (uint32_t)fo.enq_fail;
    }
    if (fo.delivered > 0) {
        s_posts_ok++;
        if (idx != (size_t)-1) {
            s_ev_posts_ok[idx]++;
            s_ev_delivered[idx] += (uint32_t)fo.delivered;
        }
    } else {
        s_posts_drop++;
        if (idx != (size_t)-1) s_ev_posts_drop[idx]++;
    }
    EV_CS_EXIT();

    return (fo.delivered > 0);
}

bool ev_post_lease(ev_src_t src, uint16_t code, lp_handle_t h, uint16_t len)
{
    const uint32_t packed = lp_pack_handle_u32(h);
    const ev_meta_t* meta = NULL;

#if defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD
    meta = ev_schema_require_known_("ev_post_lease", src, code);
    ev_schema_require_kind_1_("ev_post_lease", src, code, meta, EVK_LEASE);
    if (meta->qos != EVQ_DROP_NEW) {
        ev_schema_abort_("ev_post_lease", src, code, meta, "invalid qos for LEASE (must be DROP_NEW)");
    }
    if (packed == 0u) ev_schema_abort_("ev_post_lease", src, code, meta, "invalid lease handle");
#else
    meta = ev_meta_find(src, code);
#endif

    ev_msg_t m = { .src=src, .code=code, .a0=packed, .a1=(uint32_t)len, .t_ms=now_ms() };
    const size_t idx = meta ? (size_t)(meta - s_ev_meta) : (size_t)-1;

    const ev_fanout_t fo = ev_broadcast_lease(&m, h);
    lp_release(h);

    EV_CS_ENTER();
    if (fo.enq_fail) {
        s_enq_fail += fo.enq_fail;
        if (idx != (size_t)-1) s_ev_enq_fail[idx] += (uint32_t)fo.enq_fail;
    }
    if (fo.delivered > 0) {
        s_posts_ok++;
        if (idx != (size_t)-1) {
            s_ev_posts_ok[idx]++;
            s_ev_delivered[idx] += (uint32_t)fo.delivered;
        }
    } else {
        s_posts_drop++;
        if (idx != (size_t)-1) s_ev_posts_drop[idx]++;
    }
    EV_CS_EXIT();

    return (fo.delivered > 0);
}

bool ev_post_from_isr(ev_src_t src, uint16_t code, uint32_t a0, uint32_t a1)
{
#if defined(CONFIG_CORE_EV_SCHEMA_GUARD) && CONFIG_CORE_EV_SCHEMA_GUARD
    const ev_meta_t* meta = ev_schema_require_known_("ev_post_from_isr", src, code);
    ev_schema_require_kind_2_("ev_post_from_isr", src, code, meta, EVK_NONE, EVK_COPY);
    ev_schema_require_none_payload_("ev_post_from_isr", src, code, meta, a0, a1);
#else
    const ev_meta_t* meta = ev_meta_find(src, code);
#endif

    ev_msg_t m = { .src=src, .code=code, .a0=a0, .a1=a1, .t_ms=(uint32_t)(xTaskGetTickCountFromISR()*portTICK_PERIOD_MS) };
    const ev_qos_t qos = meta ? meta->qos : EVQ_DROP_NEW;
    const size_t idx = meta ? (size_t)(meta - s_ev_meta) : (size_t)-1;

    EV_CS_ENTER_ISR();
    uint16_t n = s_subs_cnt;
    ev_sub_t local[EV_MAX_SUBS];
    if (n > EV_MAX_SUBS) n = EV_MAX_SUBS;
    memcpy(local, s_subs, n * sizeof(ev_sub_t));
    EV_CS_EXIT_ISR();

    uint16_t delivered = 0;
    uint16_t enq_fail  = 0;
    BaseType_t hpw = pdFALSE;

    for (uint16_t i = 0; i < n; ++i) {
        if (local[i].q == NULL) continue;
        BaseType_t ok;
        if (qos == EVQ_REPLACE_LAST && local[i].depth == 1) ok = xQueueOverwriteFromISR(local[i].q, &m, &hpw);
        else ok = xQueueSendFromISR(local[i].q, &m, &hpw);

        if (ok == pdTRUE) delivered++;
        else enq_fail++;
    }

    EV_CS_ENTER_ISR();
    if (enq_fail) {
        s_enq_fail += (uint32_t)enq_fail;
        if (idx != (size_t)-1) s_ev_enq_fail[idx] += (uint32_t)enq_fail;
    }
    if (delivered > 0) {
        s_posts_ok++;
        if (idx != (size_t)-1) {
            s_ev_posts_ok[idx]++;
            s_ev_delivered[idx] += (uint32_t)delivered;
        }
    } else {
        s_posts_drop++;
        if (idx != (size_t)-1) s_ev_posts_drop[idx]++;
    }
    EV_CS_EXIT_ISR();

    if (hpw == pdTRUE) portYIELD_FROM_ISR();
    return (delivered > 0);
}

void ev_get_stats(ev_stats_t* out)
{
    if (!out) return;
    EV_CS_ENTER();
    uint16_t subs = 0;
    for (uint16_t i = 0; i < s_subs_cnt; ++i) if (s_subs[i].q) subs++;
    
    /* FIX: Aktualizacja pól struktury ev_stats_t */
    out->subs_active = subs;
    out->subs_max    = EV_MAX_SUBS;
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
    
    memset(s_ev_posts_ok,   0, sizeof(s_ev_posts_ok));
    memset(s_ev_posts_drop, 0, sizeof(s_ev_posts_drop));
    memset(s_ev_enq_fail,   0, sizeof(s_ev_enq_fail));
    memset(s_ev_delivered,  0, sizeof(s_ev_delivered));
    EV_CS_EXIT();
}

size_t ev_meta_count(void)
{
    return s_ev_meta_len;
}

const ev_meta_t* ev_meta_by_index(size_t idx)
{
    if (idx >= s_ev_meta_len) return NULL;
    return &s_ev_meta[idx];
}

size_t ev_get_event_stats(ev_event_stats_t* out, size_t max)
{
    if (!out || max == 0) return 0;
    size_t n = s_ev_meta_len;
    if (max < n) n = max;

    EV_CS_ENTER();
    for (size_t i = 0; i < n; ++i) {
        out[i].posts_ok   = s_ev_posts_ok[i];
        out[i].posts_drop = s_ev_posts_drop[i];
        out[i].enq_fail   = s_ev_enq_fail[i];
        out[i].delivered  = s_ev_delivered[i];
    }
    EV_CS_EXIT();
    return n;
}

const char* ev_kind_str(ev_kind_t kind) { return ev_kind_str_(kind); }
const char* ev_qos_str(ev_qos_t qos)    { return ev_qos_str_(qos); }

