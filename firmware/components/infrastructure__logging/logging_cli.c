/**
 * @file logging_cli.c
 * @brief CLI: logrb (ring-buffer) + loglvl (poziomy logów) + opcjonalny REPL + evstat.
 *
 * Komendy:
 *  - logrb   : diagnostyka ring‑buffer logów (stat/clear/dump/tail)
 *  - loglvl  : zmiana poziomu logowania w locie (per TAG lub globalnie '*')
 *  - evstat  : statystyki event‑busa + introspekcja schemy:
 *      - evstat
 *      - evstat --reset
 *      - evstat list [--src SRC] [--kind KIND] [--code CODE] [--name SUBSTR] [--doc] [--nohdr]
 *      - evstat show <EV_NAME|ID|SRC:CODE>
 *      - evstat check
 *
 * REPL uruchamiany jest „miękko” (idempotentnie), współdzieli UART jeśli zajęty.
 */

#include "sdkconfig.h"
#include "logging_cli.h"

#include "infra_log_rb.h"
#include "ports/log_port.h"

#if CONFIG_INFRA_LOG_CLI

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "esp_console_usb_serial_jtag.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

/* komendy: evstat + schema */
#include "core_ev.h"   // EV_SCHEMA pochodzi z core_ev.h (SSOT)

#define TAG "LOGCLI"

/* ========================= logrb ========================= */

static int cmd_logrb(int argc, char** argv)
{
#if !CONFIG_INFRA_LOG_RINGBUF
    (void)argc; (void)argv;
    printf("logrb: ring-buffer disabled (set CONFIG_INFRA_LOG_RINGBUF=y)\n");
    return 0;
#else
    if (argc < 2) {
        printf("użycie: logrb {stat|clear|dump [--limit N]|tail [N]}\n");
        return 0;
    }

    if (strcmp(argv[1], "stat") == 0) {
        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);
        printf("ringbuf: capacity=%uB used=%uB overflow=%s\n",
               (unsigned)cap, (unsigned)used, ov ? "yes":"no");
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        infra_log_rb_clear();
        printf("ringbuf: cleared\n");
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0) {
        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);

        size_t limit = used;
        if (argc >= 4 && strcmp(argv[2], "--limit") == 0) {
            limit = (size_t)strtoul(argv[3], NULL, 10);
            if (limit > used) limit = used;
        }

        char* buf = (char*)malloc(limit > 0 ? limit : 1);
        if (!buf) { printf("alloc failed\n"); return 1; }

        size_t got = 0;
        if (!infra_log_rb_snapshot(buf, limit, &got)) {
            free(buf);
            printf("snapshot failed\n"); return 1;
        }
        fwrite(buf, 1, got, stdout);
        free(buf);
        return 0;
    }

    if (strcmp(argv[1], "tail") == 0) {
        size_t n = CONFIG_INFRA_LOG_CLI_TAIL_DEFAULT;
        if (argc >= 3) {
            n = (size_t)strtoul(argv[2], NULL, 10);
            if (n == 0) n = 1;
        }

        size_t cap=0, used=0; bool ov=false;
        infra_log_rb_stat(&cap, &used, &ov);
        if (n > used) n = used;

        char* buf = (char*)malloc(n > 0 ? n : 1);
        if (!buf) { printf("alloc failed\n"); return 1; }

        size_t got = 0;
        if (!infra_log_rb_tail(buf, n, n, &got)) {
            free(buf);
            printf("tail failed\n"); return 1;
        }
        fwrite(buf, 1, got, stdout);
        free(buf);
        return 0;
    }

    printf("nieznana podkomenda: %s\n", argv[1]);
    return 0;
#endif // CONFIG_INFRA_LOG_RINGBUF
}

/* ========================= loglvl =========================
 * Zmiana poziomu logów w locie: loglvl <TAG|*> <E|W|I|D|V>
 */
static int cmd_loglvl(int argc, char** argv)
{
    if (argc < 3) {
        printf("użycie: loglvl <TAG|*> <E|W|I|D|V>\n");
        return 0;
    }
    const char* tag = argv[1];
    char lvlch = argv[2][0];

    esp_log_level_t lvl = ESP_LOG_INFO;
    switch (lvlch) {
        case 'E': lvl = ESP_LOG_ERROR;   break;
        case 'W': lvl = ESP_LOG_WARN;    break;
        case 'I': lvl = ESP_LOG_INFO;    break;
        case 'D': lvl = ESP_LOG_DEBUG;   break;
        case 'V': lvl = ESP_LOG_VERBOSE; break;
        default:
            printf("nieznany poziom: %s (użyj E/W/I/D/V)\n", argv[2]);
            return 0;
    }

    esp_log_level_set(tag, lvl);
    printf("log level for '%s' -> %c\n", tag, lvlch);
    return 0;
}

/* ========================= evstat =========================
 * Statystyki busa + introspekcja schemy (PR2.6/PR2.7/PR2.8).
 *
 *  evstat
 *  evstat --reset
 *  evstat list [--src SRC] [--kind KIND] [--code CODE] [--name SUBSTR] [--doc] [--nohdr]
 *  evstat show <EV_NAME|ID|SRC:CODE>
 *  evstat check
 */

static const char* ev_kind_str_short(ev_kind_t k)
{
    switch (k) {
        case EVK_NONE:   return "NONE";
        case EVK_COPY:   return "COPY";
        case EVK_LEASE:  return "LEASE";
        case EVK_STREAM: return "STREAM";
        default:         return "?";
    }
}

static const char* ev_src_str_short(ev_src_t src)
{
    switch (src) {
        case EV_SRC_SYS:   return "SYS";
        case EV_SRC_TIMER: return "TIMER";
        case EV_SRC_I2C:   return "I2C";
        case EV_SRC_LCD:   return "LCD";
        case EV_SRC_DS18:  return "DS18";
        case EV_SRC_LOG:   return "LOG";
        default:           return "UNK";
    }
}

static bool str_ieq_(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static bool str_icontains_(const char* hay, const char* needle)
{
    if (!needle || needle[0] == '\0') return true;
    if (!hay) return false;

    for (const char* p = hay; *p; ++p) {
        const char* h = p;
        const char* n = needle;
        while (*h && *n && (tolower((unsigned char)*h) == tolower((unsigned char)*n))) {
            ++h; ++n;
        }
        if (*n == '\0') return true;
    }
    return false;
}

static bool parse_u32_(const char* s, uint32_t* out)
{
    if (!s || !*s || !out) return false;
    errno = 0;
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 0); // base=0 => obsługa 0x...
    if (errno != 0 || end == s || *end != '\0') return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_src_(const char* s, ev_src_t* out)
{
    if (!s || !out) return false;

    uint32_t v = 0;
    if (parse_u32_(s, &v)) {
        *out = (ev_src_t)v;
        return true;
    }

    if (str_ieq_(s, "SYS"))   { *out = EV_SRC_SYS;   return true; }
    if (str_ieq_(s, "TIMER")) { *out = EV_SRC_TIMER; return true; }
    if (str_ieq_(s, "I2C"))   { *out = EV_SRC_I2C;   return true; }
    if (str_ieq_(s, "LCD"))   { *out = EV_SRC_LCD;   return true; }
    if (str_ieq_(s, "DS18") || str_ieq_(s, "DS18B20")) { *out = EV_SRC_DS18; return true; }
    if (str_ieq_(s, "LOG"))   { *out = EV_SRC_LOG;   return true; }

    return false;
}

static bool parse_kind_(const char* s, ev_kind_t* out)
{
    if (!s || !out) return false;

    uint32_t v = 0;
    if (parse_u32_(s, &v)) {
        *out = (ev_kind_t)v;
        return true;
    }

    if (str_ieq_(s, "NONE"))   { *out = EVK_NONE;   return true; }
    if (str_ieq_(s, "COPY"))   { *out = EVK_COPY;   return true; }
    if (str_ieq_(s, "LEASE"))  { *out = EVK_LEASE;  return true; }
    if (str_ieq_(s, "STREAM")) { *out = EVK_STREAM; return true; }

    return false;
}

static const char* ev_api_hint_(ev_kind_t kind)
{
    switch (kind) {
        case EVK_NONE:   return "NONE  -> ev_post(src, code, 0, 0)";
        case EVK_COPY:   return "COPY  -> ev_post(src, code, a0, a1)";
        case EVK_LEASE:  return "LEASE -> ev_post_lease(src, code, h, len)";
        case EVK_STREAM: return "STREAM -> (PR6: SPSC ring + *_READY)";
        default:         return "?";
    }
}

static void evstat_usage_(void)
{
    printf("użycie:\n");
    printf("  evstat [--reset]\n");
    printf("  evstat list [--src SRC] [--kind KIND] [--code CODE] [--name SUBSTR] [--doc] [--nohdr]\n");
    printf("  evstat show <EV_NAME|ID|SRC:CODE>\n");
    printf("  evstat check\n");
    printf("\n");
    printf("przykłady:\n");
    printf("  evstat\n");
    printf("  evstat --reset\n");
    printf("  evstat list\n");
    printf("  evstat list --doc\n");
    printf("  evstat list --src LCD\n");
    printf("  evstat list --kind LEASE\n");
    printf("  evstat list --name lcd\n");
    printf("  evstat show EV_LOG_NEW\n");
    printf("  evstat show 3\n");
    printf("  evstat show I2C:0x2000\n");
    printf("  evstat check\n");
}

static unsigned ev_schema_total_(void)
{
    unsigned n = 0;
#define X(NAME, SRC, CODE, KIND, DOC) n++;
    EV_SCHEMA(X)
#undef X
    return n;
}

/* PR2.8: lokalna “tabela” schemy do checków (SSOT: generowane z EV_SCHEMA). */
typedef struct {
    const char* name;
    ev_src_t    src;
    uint16_t    code;
    ev_kind_t   kind;
    const char* doc;
} ev_schema_row_t;

static const ev_schema_row_t s_schema_rows[] = {
#define X(NAME, SRC, CODE, KIND, DOC) { .name = #NAME, .src = (SRC), .code = (uint16_t)(CODE), .kind = EVK_##KIND, .doc = (DOC) },
    EV_SCHEMA(X)
#undef X
};

static const unsigned s_schema_rows_len = (unsigned)(sizeof(s_schema_rows) / sizeof(s_schema_rows[0]));

typedef struct {
    bool      have_src;
    ev_src_t  f_src;

    bool      have_kind;
    ev_kind_t f_kind;

    bool      have_code;
    uint16_t  f_code;

    const char* name_substr;

    bool show_doc;
    bool nohdr;

    unsigned idx;
    unsigned shown;
} evstat_list_ctx_t;

static void evstat_list_one_(evstat_list_ctx_t* c,
                             const char* name,
                             ev_src_t src,
                             uint16_t code,
                             ev_kind_t kind,
                             const char* doc)
{
    const unsigned id = c->idx++;
    if (c->have_src && src != c->f_src) return;
    if (c->have_kind && kind != c->f_kind) return;
    if (c->have_code && code != c->f_code) return;
    if (c->name_substr && !str_icontains_(name, c->name_substr)) return;

    if (c->show_doc) {
        printf("%-3u %-5s 0x%04X %-6s %-24s %s\n",
               id,
               ev_src_str_short(src),
               (unsigned)code,
               ev_kind_str_short(kind),
               name,
               (doc ? doc : ""));
    } else {
        printf("%-3u %-5s 0x%04X %-6s %s\n",
               id,
               ev_src_str_short(src),
               (unsigned)code,
               ev_kind_str_short(kind),
               name);
    }

    c->shown++;
}

static int cmd_evstat_list(int argc, char** argv)
{
    // argv[0] == "list"
    evstat_list_ctx_t c = {0};

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            evstat_usage_();
            return 0;
        }
        if (!strcmp(a, "--doc")) {
            c.show_doc = true;
            continue;
        }
        if (!strcmp(a, "--nohdr")) {
            c.nohdr = true;
            continue;
        }
        if (!strcmp(a, "--src")) {
            if (i + 1 >= argc) { printf("ERR: --src wymaga wartości\n"); return 2; }
            if (!parse_src_(argv[++i], &c.f_src)) { printf("ERR: nieznane SRC: %s\n", argv[i]); return 2; }
            c.have_src = true;
            continue;
        }
        if (!strcmp(a, "--kind")) {
            if (i + 1 >= argc) { printf("ERR: --kind wymaga wartości\n"); return 2; }
            if (!parse_kind_(argv[++i], &c.f_kind)) { printf("ERR: nieznany KIND: %s\n", argv[i]); return 2; }
            c.have_kind = true;
            continue;
        }
        if (!strcmp(a, "--code")) {
            if (i + 1 >= argc) { printf("ERR: --code wymaga wartości\n"); return 2; }
            uint32_t v = 0;
            if (!parse_u32_(argv[++i], &v) || v > 0xFFFFu) { printf("ERR: zły CODE: %s\n", argv[i]); return 2; }
            c.f_code = (uint16_t)v;
            c.have_code = true;
            continue;
        }
        if (!strcmp(a, "--name")) {
            if (i + 1 >= argc) { printf("ERR: --name wymaga wartości\n"); return 2; }
            c.name_substr = argv[++i];
            continue;
        }

        printf("ERR: nieznana opcja: %s\n", a);
        return 2;
    }

    const unsigned total = ev_schema_total_();

    if (!c.nohdr) {
        if (c.show_doc) {
            printf("id  src   code   kind   name                     doc\n");
            printf("--  ----- ------ ------ ------------------------ ------------------------------\n");
        } else {
            printf("id  src   code   kind   name\n");
            printf("--  ----- ------ ------ ------------------------\n");
        }
    }

    c.idx = 0;
    c.shown = 0;

#define X(NAME, SRC, CODE, KIND, DOC) \
    evstat_list_one_(&c, #NAME, (SRC), (uint16_t)(CODE), EVK_##KIND, (DOC));
    EV_SCHEMA(X)
#undef X

    if (!c.nohdr) {
        printf("-- shown %u / %u\n", (unsigned)c.shown, (unsigned)total);
    }
    return 0;
}

typedef enum {
    EVSHOW_BY_ID = 0,
    EVSHOW_BY_NAME,
    EVSHOW_BY_SRC_CODE,
} evshow_mode_t;

typedef struct {
    evshow_mode_t mode;

    unsigned target_id;
    const char* target_name;
    ev_src_t  target_src;
    uint16_t  target_code;

    bool found;
    unsigned id;
    const char* name;
    ev_src_t src;
    uint16_t code;
    ev_kind_t kind;
    const char* doc;

    unsigned idx;
} evstat_show_ctx_t;

static void evstat_show_one_(evstat_show_ctx_t* c,
                             const char* name,
                             ev_src_t src,
                             uint16_t code,
                             ev_kind_t kind,
                             const char* doc)
{
    const unsigned id = c->idx++;

    if (c->found) return;

    bool match = false;
    switch (c->mode) {
        case EVSHOW_BY_ID:
            match = (id == c->target_id);
            break;
        case EVSHOW_BY_NAME:
            match = str_ieq_(name, c->target_name);
            break;
        case EVSHOW_BY_SRC_CODE:
            match = (src == c->target_src && code == c->target_code);
            break;
        default:
            break;
    }

    if (match) {
        c->found = true;
        c->id    = id;
        c->name  = name;
        c->src   = src;
        c->code  = code;
        c->kind  = kind;
        c->doc   = doc;
    }
}

static int cmd_evstat_show(int argc, char** argv)
{
    // argv[0] == "show"
    if (argc < 2) {
        printf("ERR: brak argumentu\n");
        evstat_usage_();
        return 2;
    }

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        evstat_usage_();
        return 0;
    }

    const char* key = argv[1];
    const unsigned total = ev_schema_total_();

    evstat_show_ctx_t c = {0};
    c.idx = 0;

    uint32_t v = 0;

    // 1) ID liczbowy
    if (parse_u32_(key, &v) && v < total) {
        c.mode = EVSHOW_BY_ID;
        c.target_id = (unsigned)v;
    } else {
        // 2) SRC:CODE
        const char* colon = strchr(key, ':');
        if (colon) {
            char srcbuf[16] = {0};
            size_t slen = (size_t)(colon - key);
            if (slen == 0 || slen >= sizeof(srcbuf)) {
                printf("ERR: zły format SRC:CODE (SRC za długi)\n");
                return 2;
            }
            memcpy(srcbuf, key, slen);
            srcbuf[slen] = '\0';

            ev_src_t src = 0;
            if (!parse_src_(srcbuf, &src)) {
                printf("ERR: nieznane SRC: %s\n", srcbuf);
                return 2;
            }

            uint32_t code_u = 0;
            if (!parse_u32_(colon + 1, &code_u) || code_u > 0xFFFFu) {
                printf("ERR: zły CODE: %s\n", colon + 1);
                return 2;
            }

            c.mode = EVSHOW_BY_SRC_CODE;
            c.target_src  = src;
            c.target_code = (uint16_t)code_u;
        } else {
            // 3) Nazwa eventu
            c.mode = EVSHOW_BY_NAME;
            c.target_name = key;
        }
    }

#define X(NAME, SRC, CODE, KIND, DOC) \
    evstat_show_one_(&c, #NAME, (SRC), (uint16_t)(CODE), EVK_##KIND, (DOC));
    EV_SCHEMA(X)
#undef X

    if (!c.found) {
        printf("ERR: nie znaleziono eventu: %s\n", key);
        printf("  użyj: evstat list\n");
        return 1;
    }

    printf("EV[%u] %s\n", (unsigned)c.id, c.name ? c.name : "?");
    printf("  src : %s (0x%04X)\n", ev_src_str_short(c.src), (unsigned)c.src);
    printf("  code: 0x%04X\n", (unsigned)c.code);
    printf("  kind: %s (%u)\n", ev_kind_str_short(c.kind), (unsigned)c.kind);
    if (c.doc && c.doc[0]) {
        printf("  doc : %s\n", c.doc);
    }
    printf("  api : %s\n", ev_api_hint_(c.kind));

    return 0;
}

/* PR2.8: “krytyczność” — prosta heurystyka po nazwie (bez dodatkowych flag w schemie). */
static bool ev_is_critical_name_(const char* name)
{
    if (!name) return false;

    // Krytyczne: *_ERROR*, *_START* oraz EV_LOG_NEW (log-line jest API systemowe).
    if (str_icontains_(name, "ERROR")) return true;
    if (str_icontains_(name, "START")) return true;
    if (str_ieq_(name, "EV_LOG_NEW")) return true;

    return false;
}

static int cmd_evstat_check(int argc, char** argv)
{
    // argv[0] == "check"
    if (argc >= 2) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            evstat_usage_();
            return 0;
        }
        printf("ERR: evstat check nie przyjmuje argumentów\n");
        evstat_usage_();
        return 2;
    }

    int issues = 0;

    // 1) duplikaty (src,code)
    for (unsigned i = 0; i < s_schema_rows_len; ++i) {
        for (unsigned j = i + 1; j < s_schema_rows_len; ++j) {
            if (s_schema_rows[i].src == s_schema_rows[j].src &&
                s_schema_rows[i].code == s_schema_rows[j].code) {

                printf("FAIL dup src+code: %s:0x%04X : %s <-> %s\n",
                       ev_src_str_short(s_schema_rows[i].src),
                       (unsigned)s_schema_rows[i].code,
                       s_schema_rows[i].name ? s_schema_rows[i].name : "?",
                       s_schema_rows[j].name ? s_schema_rows[j].name : "?");
                issues++;
            }
        }
    }

    // 2) duplikaty name
    for (unsigned i = 0; i < s_schema_rows_len; ++i) {
        if (!s_schema_rows[i].name) continue;
        for (unsigned j = i + 1; j < s_schema_rows_len; ++j) {
            if (!s_schema_rows[j].name) continue;
            if (strcmp(s_schema_rows[i].name, s_schema_rows[j].name) == 0) {
                printf("FAIL dup name: %s (%s:0x%04X) and (%s:0x%04X)\n",
                       s_schema_rows[i].name,
                       ev_src_str_short(s_schema_rows[i].src),
                       (unsigned)s_schema_rows[i].code,
                       ev_src_str_short(s_schema_rows[j].src),
                       (unsigned)s_schema_rows[j].code);
                issues++;
            }
        }
    }

    // 3) sanity per-entry: name/kind/doc (dla krytycznych)
    for (unsigned i = 0; i < s_schema_rows_len; ++i) {
        const ev_schema_row_t* e = &s_schema_rows[i];

        if (!e->name || e->name[0] == '\0') {
            printf("FAIL empty name: idx=%u src=%s code=0x%04X\n",
                   i,
                   ev_src_str_short(e->src),
                   (unsigned)e->code);
            issues++;
        }

        if ((unsigned)e->kind > (unsigned)EVK_STREAM) {
            printf("FAIL invalid kind: idx=%u name=%s kind=%u\n",
                   i,
                   e->name ? e->name : "?",
                   (unsigned)e->kind);
            issues++;
        }

        if (ev_is_critical_name_(e->name)) {
            if (!e->doc || e->doc[0] == '\0') {
                printf("FAIL missing doc (CRITICAL): name=%s src=%s code=0x%04X kind=%s\n",
                       e->name ? e->name : "?",
                       ev_src_str_short(e->src),
                       (unsigned)e->code,
                       ev_kind_str_short(e->kind));
                issues++;
            }
        }
    }

    if (issues == 0) {
        printf("evstat check: OK (entries=%u)\n", (unsigned)s_schema_rows_len);
        return 0;
    }

    printf("evstat check: FAIL (issues=%d, entries=%u)\n", issues, (unsigned)s_schema_rows_len);
    return 1;
}

static int cmd_evstat(int argc, char **argv)
{
    // Help
    if (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        evstat_usage_();
        return 0;
    }

    // Reset statów
    if (argc >= 2 && !strcmp(argv[1], "--reset")) {
        if (argc != 2) {
            printf("ERR: --reset nie przyjmuje dodatkowych argumentów\n");
            evstat_usage_();
            return 2;
        }
        ev_reset_stats();
        printf("ev: stats reset\n");
        return 0;
    }

    // Subkomendy: list / show / check
    if (argc >= 2) {
        if (!strcmp(argv[1], "list")) {
            return cmd_evstat_list(argc - 1, argv + 1);
        }
        if (!strcmp(argv[1], "show")) {
            return cmd_evstat_show(argc - 1, argv + 1);
        }
        if (!strcmp(argv[1], "check")) {
            return cmd_evstat_check(argc - 1, argv + 1);
        }
    }

    // Statystyki busa (tryb domyślny) — tylko bez argumentów
    if (argc == 1) {
        ev_stats_t s;
        ev_get_stats(&s);
        printf("ev: subs=%u q_depth_max=%u posts_ok=%u posts_drop=%u enq_fail=%u\n",
               (unsigned)s.subs, (unsigned)s.q_depth_max,
               (unsigned)s.posts_ok, (unsigned)s.posts_drop, (unsigned)s.enq_fail);
        return 0;
    }

    // Nieznany tryb/opcja
    printf("ERR: nieznany tryb/opcja: %s\n", argv[1]);
    evstat_usage_();
    return 2;
}

/* ==================== rejestracja CLI (idempotentna) ==================== */

static bool s_cmds_registered = false;

esp_err_t infra_log_cli_register(void)
{
    if (s_cmds_registered) {
        ESP_LOGD(TAG, "commands already registered");
        return ESP_OK;
    }

    // help – również „miękko”
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_register_help_command());

    const esp_console_cmd_t cmd_logrb_desc = {
        .command = "logrb",
        .help    = "logrb stat|clear|dump [--limit N]|tail [N]",
        .hint    = NULL,
        .func    = &cmd_logrb,
        .argtable= NULL
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_logrb_desc));

    const esp_console_cmd_t cmd_loglvl_desc = {
        .command = "loglvl",
        .help    = "loglvl <TAG|*> <E|W|I|D|V>  (zmień poziom logów w locie)",
        .hint    = NULL,
        .func    = &cmd_loglvl,
        .argtable= NULL
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_loglvl_desc));

    const esp_console_cmd_t cmd_evstat_desc = {
        .command = "evstat",
        .help    =
            "Event-bus stats + schema.\n"
            "Usage: evstat [--reset] | evstat list [...] | evstat show <EV_NAME|ID|SRC:CODE> | evstat check",
        .hint    = NULL,
        .func    = &cmd_evstat,
        .argtable= NULL
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_evstat_desc));

    s_cmds_registered = true;
    ESP_LOGI(TAG, "registered 'logrb', 'loglvl' and 'evstat' commands");
    return ESP_OK;
}

/* ========================== REPL (idempotentny/miękki) ========================= */

static esp_console_repl_t* s_repl = NULL;
static SemaphoreHandle_t   s_cli_mux = NULL;

static inline void ensure_mux_(void)
{
    if (!s_cli_mux) {
        s_cli_mux = xSemaphoreCreateMutex();
    }
}

esp_err_t infra_log_cli_start_repl(void)
{
#if !CONFIG_INFRA_LOG_CLI_START_REPL
    // Rejestrujemy tylko komendy, bez odpalania REPL
    return infra_log_cli_register();
#else
    ensure_mux_();
    if (!s_cli_mux) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_cli_mux, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "REPL mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (s_repl) {  // już działa
        xSemaphoreGive(s_cli_mux);
        return ESP_OK;
    }

    // REPL konfiguracja
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.prompt = "esp> ";

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // USB-Serial-JTAG: nie używa sterownika UART
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t* repl = NULL;
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_new_repl_usb_serial_jtag() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }
    // Komendy (idempotentnie)
    infra_log_cli_register();

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_start_repl() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }
    s_repl = repl;
    ESP_LOGI(TAG, "USB-Serial-JTAG REPL started — wpisz komendy (np. 'logrb stat').");
    xSemaphoreGive(s_cli_mux);
    return ESP_OK;

#else
    // UART REPL — „miękko” współdzieli się z innymi
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    #if defined(CONFIG_ESP_CONSOLE_UART_NUM)
        const uart_port_t cli_uart = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
        uart_cfg.channel = cli_uart;
    #else
        const uart_port_t cli_uart = UART_NUM_0;
        uart_cfg.channel = UART_NUM_0;
    #endif

    if (uart_is_driver_installed(cli_uart)) {
        ESP_LOGW(TAG, "UART driver already installed — skip starting second REPL");
        infra_log_cli_register();
        xSemaphoreGive(s_cli_mux);
        return ESP_OK;
    }

    esp_console_repl_t* repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_new_repl_uart() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    infra_log_cli_register();

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_start_repl() failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_cli_mux);
        return err;
    }

    s_repl = repl;
    ESP_LOGI(TAG, "UART REPL started — wpisz komendy (np. 'logrb stat').");
    xSemaphoreGive(s_cli_mux);
    return ESP_OK;
#endif // CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

#endif // CONFIG_INFRA_LOG_CLI_START_REPL
}

#else   // !CONFIG_INFRA_LOG_CLI

// Stuby kiedy CLI jest wyłączone w Kconfig — wygodnie dla użytkowników API
esp_err_t infra_log_cli_register(void)   { return ESP_OK; }
esp_err_t infra_log_cli_start_repl(void) { return ESP_OK; }

#endif  // CONFIG_INFRA_LOG_CLI
