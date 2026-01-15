#include "sdkconfig.h"
#include "logging_cli.h"

#include "infra_log_rb.h"
#include "ports/log_port.h"
#include "ports/spi_port.h" // Obsługa SPI

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
#include <inttypes.h>

/* komendy: evstat + schema */
#include "core_ev.h"   // EV_SCHEMA pochodzi z core_ev.h (SSOT)
#include "core/leasepool.h" // Do alokacji payloadu dla UART

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

/* ========================= loglvl ========================= */
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

/* ========================= evstat ========================= */

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
        case EV_SRC_UART:  return "UART";
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
    unsigned long v = strtoul(s, &end, 0); 
    if (errno != 0 || end == s || *end != '\0') return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_src_(const char* s, ev_src_t* out)
{
    if (!s || !out) return false;
    uint32_t v = 0;
    if (parse_u32_(s, &v)) { *out = (ev_src_t)v; return true; }
    if (str_ieq_(s, "SYS"))   { *out = EV_SRC_SYS;   return true; }
    if (str_ieq_(s, "TIMER")) { *out = EV_SRC_TIMER; return true; }
    if (str_ieq_(s, "I2C"))   { *out = EV_SRC_I2C;   return true; }
    if (str_ieq_(s, "LCD"))   { *out = EV_SRC_LCD;   return true; }
    if (str_ieq_(s, "DS18") || str_ieq_(s, "DS18B20")) { *out = EV_SRC_DS18; return true; }
    if (str_ieq_(s, "LOG"))   { *out = EV_SRC_LOG;   return true; }
    if (str_ieq_(s, "UART"))  { *out = EV_SRC_UART;  return true; }
    return false;
}

static bool parse_kind_(const char* s, ev_kind_t* out)
{
    if (!s || !out) return false;
    uint32_t v = 0;
    if (parse_u32_(s, &v)) { *out = (ev_kind_t)v; return true; }
    if (str_ieq_(s, "NONE"))   { *out = EVK_NONE;   return true; }
    if (str_ieq_(s, "COPY"))   { *out = EVK_COPY;   return true; }
    if (str_ieq_(s, "LEASE"))  { *out = EVK_LEASE;  return true; }
    if (str_ieq_(s, "STREAM")) { *out = EVK_STREAM; return true; }
    return false;
}

static const char* ev_api_hint_(ev_kind_t kind, ev_qos_t qos)
{
    switch (kind) {
        case EVK_NONE:
            return (qos == EVQ_REPLACE_LAST) ? "NONE (REPLACE_LAST) -> ev_post(src, code, 0, 0) + sub depth=1" : "NONE  -> ev_post(src, code, 0, 0)";
        case EVK_COPY:
            return (qos == EVQ_REPLACE_LAST) ? "COPY (REPLACE_LAST) -> ev_post(src, code, a0, a1) + sub depth=1" : "COPY  -> ev_post(src, code, a0, a1)";
        case EVK_LEASE:  return "LEASE -> ev_post_lease(src, code, h, len)";
        case EVK_STREAM: return "STREAM -> ev_post(src, code, 0,0) + drain SPSC ring (peek/consume)";
        default:         return "?";
    }
}

static void evstat_usage_(void)
{
    printf("użycie:\n evstat [--reset] | stat [--per-event] | list [...] | show <ID> | check\n");
}

static unsigned ev_schema_total_(void)
{
    unsigned n = 0;
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) n++;
    EV_SCHEMA(X)
#undef X
    return n;
}

typedef struct {
    const char* name; ev_src_t src; uint16_t code; ev_kind_t kind; ev_qos_t qos; uint16_t flags; const char* doc;
} ev_schema_row_t;

static const ev_schema_row_t s_schema_rows[] = {
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) { .name = #NAME, .src = (SRC), .code = (uint16_t)(CODE), .kind = EVK_##KIND, .qos = EVQ_##QOS, .flags = (uint16_t)(FLAGS), .doc = (DOC) },
    EV_SCHEMA(X)
#undef X
};
static const unsigned s_schema_rows_len = (unsigned)(sizeof(s_schema_rows) / sizeof(s_schema_rows[0]));

/* --- evstat list helper --- */
typedef struct {
    bool have_src; ev_src_t f_src;
    bool have_kind; ev_kind_t f_kind;
    bool have_code; uint16_t f_code;
    const char* name_substr;
    bool show_doc; bool show_qos; bool nohdr;
    unsigned idx; unsigned shown;
} evstat_list_ctx_t;

static void evstat_list_one_(evstat_list_ctx_t* c, const char* name, ev_src_t src, uint16_t code, ev_kind_t kind, ev_qos_t qos, const char* doc)
{
    const unsigned id = c->idx++;
    if (c->have_src && src != c->f_src) return;
    if (c->have_kind && kind != c->f_kind) return;
    if (c->have_code && code != c->f_code) return;
    if (c->name_substr && !str_icontains_(name, c->name_substr)) return;

    if (!c->nohdr && c->shown == 0) {
        printf("id  src   code   kind   name\n");
        printf("--  ----- ------ ------ ------------------------\n");
    }

    /* Rzutowanie na unsigned, aby uniknąć błędów formatowania */
    printf("%-3u %-5s 0x%04X %-6s %s\n", (unsigned)id, ev_src_str_short(src), (unsigned)code, ev_kind_str_short(kind), name);
    (void)qos; (void)doc; // używane w pełnej wersji, tu uciszamy warning
    c->shown++;
}

static int cmd_evstat_list(int argc, char** argv)
{
    evstat_list_ctx_t c = {0};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--src") && i+1<argc) { parse_src_(argv[++i], &c.f_src); c.have_src=true; }
        else if (!strcmp(argv[i], "--doc")) c.show_doc = true;
    }
    c.idx = 0; c.shown = 0;
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) evstat_list_one_(&c, #NAME, (SRC), (uint16_t)(CODE), EVK_##KIND, EVQ_##QOS, (DOC));
    EV_SCHEMA(X)
#undef X
    return 0;
}

static int cmd_evstat_stat(int argc, char** argv)
{
    bool per_event = false;
    if (argc > 1 && strcmp(argv[1], "--per-event") == 0) per_event = true;

    ev_stats_t s;
    ev_get_stats(&s);
    printf("evstat: subs=%u (max=%u) depth_max=%u total_ev=%u\n",
           (unsigned)s.subs_active, (unsigned)s.subs_max, (unsigned)s.q_depth_max, (unsigned)ev_schema_total_());
    printf("  posts_ok=%u posts_drop=%u enq_fail=%u\n",
           (unsigned)s.posts_ok, (unsigned)s.posts_drop, (unsigned)s.enq_fail);

    if (per_event) {
        ev_event_stats_t* st = calloc(s_schema_rows_len, sizeof(*st));
        if (st) {
            ev_get_event_stats(st, s_schema_rows_len);
            printf("id  src   code   posts_ok   name\n");
            for(unsigned i=0; i<s_schema_rows_len; ++i) {
                 // FIX: Rzutowanie posts_ok na unsigned
                 printf("%-3u %-5s 0x%04X %-10u %s\n", (unsigned)i, ev_src_str_short(s_schema_rows[i].src), 
                        (unsigned)s_schema_rows[i].code, (unsigned)st[i].posts_ok, s_schema_rows[i].name);
            }
            free(st);
        }
    }
    return 0;
}

static int cmd_evstat(int argc, char **argv)
{
    if (argc < 2) return cmd_evstat_stat(argc, argv);
    if (!strcmp(argv[1], "stat")) return cmd_evstat_stat(argc-1, argv+1);
    if (!strcmp(argv[1], "list")) return cmd_evstat_list(argc-1, argv+1);
    if (!strcmp(argv[1], "check")) { printf("evstat check: OK (stub)\n"); return 0; }
    evstat_usage_();
    return 0;
}

/* ===================== komendy: lpstat ===================== */
static int cmd_lpstat(int argc, char **argv)
{
    (void)argc; (void)argv;
    lp_stats_t st = {0};
    lp_get_stats(&st);
    printf("lp: total=%u free=%u used=%u peak=%u alloc_ok=%u alloc_fail=%u guard_fail=%u\n",
           (unsigned)st.slots_total, (unsigned)st.slots_free,
           (unsigned)st.slots_used, (unsigned)st.slots_peak_used,
           (unsigned)st.alloc_ok, (unsigned)st.drops_alloc_fail,
           (unsigned)st.guard_failures);
    return 0;
}

/* ===================== komenda: uart_send ===================== */
static int cmd_uart_send(int argc, char** argv)
{
    if (argc < 2) { printf("użycie: uart_send <tekst>\n"); return 0; }
    const char* msg = argv[1];
    size_t len = strlen(msg);
    lp_handle_t h = lp_alloc_try((uint32_t)len + 1);
    if (lp_handle_is_valid(h)) {
        lp_view_t v;
        if (lp_acquire(h, &v)) {
            memcpy(v.ptr, msg, len); ((char*)v.ptr)[len] = 0;
            lp_commit(h, (uint32_t)len);
            ev_post_lease(EV_SRC_UART, EV_UART_TX_REQ, h, (uint16_t)len);
            printf("Wysłano %u bajtów na UART (EV_UART_TX_REQ)\n", (unsigned)len);
        } else {
            lp_release(h);
        }
    } else {
        printf("ERR: LeasePool full\n");
    }
    return 0;
}

/* ===================== komenda: spi_test ===================== */
static spi_bus_t* s_test_spi_bus = NULL;

static int cmd_spi_test(int argc, char** argv) {
    (void)argc; (void)argv;
    if (s_test_spi_bus) {
        printf("SPI already init\n");
        return 0;
    }
    spi_bus_cfg_t cfg = {
        .mosi_io = 19, .miso_io = 20, .sclk_io = 21,
        .max_transfer_sz = 128, .enable_dma = true, .host_id = 0
    };
    port_err_t err = spi_bus_create(&cfg, &s_test_spi_bus);
    if (err == PORT_OK) {
        printf("SPI Bus Init: SUCCESS (MOSI=19, MISO=20, CLK=21, DMA=ON)\n");
    } else {
        printf("SPI Bus Init: FAILED (err=%d)\n", (int)err);
    }
    return 0;
}

/* ==================== rejestracja CLI ==================== */

static bool s_cmds_registered = false;

esp_err_t infra_log_cli_register(void)
{
    if (s_cmds_registered) return ESP_OK;

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_register_help_command());

    // Zmiana nazw zmiennych, aby uniknąć konfliktu z funkcjami (shadowing)
    const esp_console_cmd_t c_logrb = { .command="logrb", .help="logrb stat|clear|dump|tail", .func=&cmd_logrb };
    esp_console_cmd_register(&c_logrb);

    const esp_console_cmd_t c_loglvl = { .command="loglvl", .help="loglvl <TAG> <L>", .func=&cmd_loglvl };
    esp_console_cmd_register(&c_loglvl);

    const esp_console_cmd_t c_evstat = { .command="evstat", .help="evstat stat|list|check", .func=&cmd_evstat };
    esp_console_cmd_register(&c_evstat);

    const esp_console_cmd_t c_lpstat = { .command="lpstat", .help="lpstat", .func=&cmd_lpstat };
    esp_console_cmd_register(&c_lpstat);

    const esp_console_cmd_t c_uart = { .command="uart_send", .help="uart_send <msg>", .func=&cmd_uart_send };
    esp_console_cmd_register(&c_uart);

    const esp_console_cmd_t c_spi = { .command="spi_test", .help="Init SPI bus to verify driver", .func=&cmd_spi_test };
    esp_console_cmd_register(&c_spi);

    s_cmds_registered = true;
    ESP_LOGI(TAG, "CLI commands registered: logrb, loglvl, evstat, lpstat, uart_send, spi_test");
    return ESP_OK;
}

/* ========================== REPL ========================= */

static esp_console_repl_t* s_repl = NULL;
static SemaphoreHandle_t   s_cli_mux = NULL;

esp_err_t infra_log_cli_start_repl(void)
{
#if !CONFIG_INFRA_LOG_CLI_START_REPL
    return infra_log_cli_register();
#else
    if (!s_cli_mux) s_cli_mux = xSemaphoreCreateMutex();
    if (xSemaphoreTake(s_cli_mux, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_repl) { xSemaphoreGive(s_cli_mux); return ESP_OK; }

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.prompt = "esp> ";

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_t* repl = NULL;
    esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl);
    infra_log_cli_register();
    esp_console_start_repl(repl);
    s_repl = repl;
#else
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    #if defined(CONFIG_ESP_CONSOLE_UART_NUM)
        uart_cfg.channel = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    #else
        uart_cfg.channel = UART_NUM_0;
    #endif

    if (!uart_is_driver_installed(uart_cfg.channel)) {
        esp_console_repl_t* repl = NULL;
        esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
        infra_log_cli_register();
        esp_console_start_repl(repl);
        s_repl = repl;
    } else {
        infra_log_cli_register(); // UART zajęty, rejestrujemy tylko komendy
    }
#endif
    ESP_LOGI(TAG, "REPL started.");
    xSemaphoreGive(s_cli_mux);
    return ESP_OK;
#endif
}

#else   // !CONFIG_INFRA_LOG_CLI
esp_err_t infra_log_cli_register(void)   { return ESP_OK; }
esp_err_t infra_log_cli_start_repl(void) { return ESP_OK; }
#endif  // CONFIG_INFRA_LOG_CLI
