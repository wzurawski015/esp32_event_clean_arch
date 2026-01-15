#include "sdkconfig.h"
#include "logging_cli.h"

#include "infra_log_rb.h"
#include "ports/log_port.h"
#include "ports/spi_port.h"

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

#include "core_ev.h"
#include "core/leasepool.h"

#define TAG "LOGCLI"

/* ========================= logrb ========================= */
static int cmd_logrb(int argc, char** argv) {
    if (argc < 2) { printf("użycie: logrb {stat|clear|dump|tail}\n"); return 0; }
    if (strcmp(argv[1], "stat") == 0) {
        size_t c, u; bool o; infra_log_rb_stat(&c, &u, &o);
        printf("rb: cap=%u used=%u ov=%d\n", (unsigned)c, (unsigned)u, (int)o);
    } else if (strcmp(argv[1], "clear") == 0) { infra_log_rb_clear(); }
    else if (strcmp(argv[1], "tail") == 0) {
        size_t n = 500;
        if(argc>2) n=strtoul(argv[2],NULL,0);
        char* b = malloc(n);
        if(b) {
            size_t g;
            if(infra_log_rb_tail(b, n, n, &g)) fwrite(b, 1, g, stdout);
            free(b);
        }
    }
    return 0;
}

/* ========================= loglvl ========================= */
static int cmd_loglvl(int argc, char** argv) {
    if (argc < 3) return 0;
    esp_log_level_set(argv[1], (argv[2][0] == 'D') ? ESP_LOG_DEBUG : ESP_LOG_INFO);
    return 0;
}

/* ========================= evstat helpers ========================= */
static const char* ev_kind_str_short(ev_kind_t k) {
    switch (k) {
        case EVK_NONE:   return "NONE";
        case EVK_COPY:   return "COPY";
        case EVK_LEASE:  return "LEASE";
        case EVK_STREAM: return "STREAM";
        default:         return "?";
    }
}
static const char* ev_src_str_short(ev_src_t src) {
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
static bool str_ieq_(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}
static bool str_icontains_(const char* hay, const char* needle) {
    if (!needle || !*needle) return true;
    if (!hay) return false;
    // prosty strcasestr
    for(; *hay; hay++) {
        const char *h=hay, *n=needle;
        while(*h && *n && tolower((unsigned char)*h)==tolower((unsigned char)*n)) { h++; n++; }
        if(!*n) return true;
    }
    return false;
}
static bool parse_u32_(const char* s, uint32_t* out) {
    char* end; *out = strtoul(s, &end, 0); return (end > s && *end == 0);
}
static bool parse_src_(const char* s, ev_src_t* out) {
    uint32_t v; if(parse_u32_(s, &v)) { *out=(ev_src_t)v; return true; }
    if(str_ieq_(s,"SYS")) { *out=EV_SRC_SYS; return true; }
    if(str_ieq_(s,"I2C")) { *out=EV_SRC_I2C; return true; }
    if(str_ieq_(s,"LCD")) { *out=EV_SRC_LCD; return true; }
    if(str_ieq_(s,"DS18")){ *out=EV_SRC_DS18;return true; }
    if(str_ieq_(s,"UART")){ *out=EV_SRC_UART;return true; }
    if(str_ieq_(s,"LOG")) { *out=EV_SRC_LOG; return true; }
    if(str_ieq_(s,"TIMER")){*out=EV_SRC_TIMER;return true;}
    return false;
}
static bool parse_kind_(const char* s, ev_kind_t* out) {
    uint32_t v; if(parse_u32_(s, &v)) { *out=(ev_kind_t)v; return true; }
    if(str_ieq_(s,"NONE")) { *out=EVK_NONE; return true; }
    if(str_ieq_(s,"COPY")) { *out=EVK_COPY; return true; }
    if(str_ieq_(s,"LEASE")){ *out=EVK_LEASE;return true; }
    if(str_ieq_(s,"STREAM")){*out=EVK_STREAM;return true;}
    return false;
}
static const char* ev_api_hint_(ev_kind_t k, ev_qos_t q) {
    if(k==EVK_NONE) return "ev_post(src,code,0,0)";
    if(k==EVK_COPY) return "ev_post(src,code,a0,a1)";
    if(k==EVK_LEASE) return "ev_post_lease(...)";
    if(k==EVK_STREAM) return "stream write -> ev_post";
    return "?";
}

/* ========================= evstat commands ========================= */
static unsigned ev_total_(void) { unsigned n=0; 
#define X(...) n++; 
EV_SCHEMA(X) 
#undef X 
return n; }

typedef struct { bool has_src; ev_src_t src; bool has_kind; ev_kind_t kind; const char* name_sub; } ev_filter_t;

static void evstat_list_item(ev_filter_t* f, const char* name, ev_src_t src, uint16_t code, ev_kind_t kind) {
    if(f->has_src && f->src != src) return;
    if(f->has_kind && f->kind != kind) return;
    if(f->name_sub && !str_icontains_(name, f->name_sub)) return;
    printf("  %-20s src=%s(0x%02X) code=0x%04X kind=%s\n", name, ev_src_str_short(src), src, code, ev_kind_str_short(kind));
}

static int cmd_evstat_list(int argc, char** argv) {
    ev_filter_t f={0};
    for(int i=1; i<argc; ++i) {
        if(!strcmp(argv[i],"--src") && i+1<argc) { parse_src_(argv[++i], &f.src); f.has_src=true; }
        if(!strcmp(argv[i],"--kind") && i+1<argc) { parse_kind_(argv[++i], &f.kind); f.has_kind=true; }
        if(!strcmp(argv[i],"--name") && i+1<argc) { f.name_sub = argv[++i]; }
    }
#define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC) evstat_list_item(&f, #NAME, SRC, CODE, EVK_##KIND);
    EV_SCHEMA(X)
#undef X
    return 0;
}

static int cmd_evstat_stat(int argc, char** argv) {
    (void)argc; (void)argv;
    ev_stats_t s; ev_get_stats(&s);
    printf("EV: subs=%u posts=%u drops=%u\n", s.subs_active, s.posts_ok, s.posts_drop);
    return 0;
}

static int cmd_evstat_show(int argc, char** argv) {
    if(argc<2) return 0;
    // uproszczone: szukamy po nazwie
    const char* target = argv[1];
    #define X(NAME, SRC, CODE, KIND, QOS, FLAGS, DOC)         if(str_ieq_(#NAME, target)) {             printf("Found: %s\n Doc: %s\n API: %s\n", #NAME, DOC, ev_api_hint_(EVK_##KIND, EVQ_##QOS));             return 0;         }
    EV_SCHEMA(X)
    #undef X
    printf("Event not found\n");
    return 0;
}

static int cmd_evstat(int argc, char** argv) {
    if(argc<2) return cmd_evstat_stat(0,NULL);
    if(!strcmp(argv[1],"list")) return cmd_evstat_list(argc-1, argv+1);
    if(!strcmp(argv[1],"stat")) return cmd_evstat_stat(argc-1, argv+1);
    if(!strcmp(argv[1],"show")) return cmd_evstat_show(argc-1, argv+1);
    return 0;
}

/* ========================= lpstat ========================= */
static int cmd_lpstat(int argc, char** argv) {
    (void)argc; (void)argv;
    lp_stats_t s; lp_get_stats(&s);
    printf("LP: free=%u used=%u peak=%u fail=%u\n", s.slots_free, s.slots_used, s.slots_peak_used, s.drops_alloc_fail);
    return 0;
}

/* ========================= uart_send ========================= */
static int cmd_uart_send(int argc, char** argv) {
    if(argc<2) return 0;
    size_t len = strlen(argv[1]);
    lp_handle_t h = lp_alloc_try(len+1);
    if(lp_handle_is_valid(h)) {
        lp_view_t v; lp_acquire(h, &v);
        memcpy(v.ptr, argv[1], len); ((char*)v.ptr)[len]=0;
        lp_commit(h, len);
        ev_post_lease(EV_SRC_UART, EV_UART_TX_REQ, h, len);
        printf("Sent %zu bytes\n", len);
    } else printf("LP Full\n");
    return 0;
}

/* ========================= spi_test ========================= */
static spi_bus_t* s_test_spi = NULL;
static int cmd_spi_test(int argc, char** argv) {
    (void)argc; (void)argv;
    if(s_test_spi) { printf("Already init\n"); return 0; }
    spi_bus_cfg_t cfg = { .mosi_io=19, .miso_io=20, .sclk_io=21, .max_transfer_sz=64, .enable_dma=true, .host_id=1 };
    if(spi_bus_create(&cfg, &s_test_spi)==0) printf("SPI Init OK (Host=1)\n");
    else printf("SPI Init Fail\n");
    return 0;
}

/* ========================= Register ========================= */
esp_err_t infra_log_cli_register(void) {
    esp_console_register_help_command();
    const esp_console_cmd_t c1 = { .command="logrb", .func=&cmd_logrb }; esp_console_cmd_register(&c1);
    const esp_console_cmd_t c2 = { .command="loglvl", .func=&cmd_loglvl }; esp_console_cmd_register(&c2);
    const esp_console_cmd_t c3 = { .command="evstat", .func=&cmd_evstat }; esp_console_cmd_register(&c3);
    const esp_console_cmd_t c4 = { .command="lpstat", .func=&cmd_lpstat }; esp_console_cmd_register(&c4);
    const esp_console_cmd_t c5 = { .command="uart_send", .func=&cmd_uart_send }; esp_console_cmd_register(&c5);
    const esp_console_cmd_t c6 = { .command="spi_test", .func=&cmd_spi_test }; esp_console_cmd_register(&c6);
    return ESP_OK;
}

static SemaphoreHandle_t s_mux = NULL;
esp_err_t infra_log_cli_start_repl(void) {
    if(!s_mux) s_mux = xSemaphoreCreateMutex();
    if(xSemaphoreTake(s_mux, 0)!=pdTRUE) return ESP_OK; // already running
    
    esp_console_repl_config_t rcfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rcfg.prompt = "esp> ";
    infra_log_cli_register();
    
    #if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dcfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_t* r; esp_console_new_repl_usb_serial_jtag(&dcfg, &rcfg, &r);
    esp_console_start_repl(r);
    #else
    esp_console_dev_uart_config_t dcfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if(!uart_is_driver_installed(UART_NUM_0)) {
        esp_console_repl_t* r; esp_console_new_repl_uart(&dcfg, &rcfg, &r);
        esp_console_start_repl(r);
    }
    #endif
    return ESP_OK;
}
#else
esp_err_t infra_log_cli_register(void){return 0;}
esp_err_t infra_log_cli_start_repl(void){return 0;}
#endif
