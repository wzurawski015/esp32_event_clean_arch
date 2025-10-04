#include "ports/log_port.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>

static int s_threshold = 4; // 0..4: ERROR..VERBOSE

static int cfg2level(void) {
#if   CONFIG_INFRA_LOG_LEVEL_ERROR
  return 0;
#elif CONFIG_INFRA_LOG_LEVEL_WARN
  return 1;
#elif CONFIG_INFRA_LOG_LEVEL_INFO
  return 2;
#elif CONFIG_INFRA_LOG_LEVEL_DEBUG
  return 3;
#else
  return 4; // VERBOSE
#endif
}

__attribute__((constructor))
static void _init_log_threshold(void) { s_threshold = cfg2level(); }

#if CONFIG_INFRA_LOG_RINGBUF
#include <stdlib.h>
#include <string.h>
#include "esp_rom_sys.h"
static char* s_rb;
static size_t s_rb_sz;
static volatile size_t s_w;

__attribute__((constructor))
static void _init_rb(void) {
  s_rb_sz = (size_t)CONFIG_INFRA_LOG_RINGBUF_KB * 1024;
  s_rb = (char*)malloc(s_rb_sz);
}

static void rb_push(const char* tag, log_level_t lvl, const char* fmt, va_list ap) {
  if (!s_rb || s_rb_sz < 32) return;
  char line[160];
  int n = esp_rom_vsnprintf(line, sizeof(line), fmt, ap);
  char head[32];
  int hn = snprintf(head, sizeof(head), "[%d]%s: ", (int)lvl, tag ? tag : "");
  size_t tot = (size_t)((hn>0?hn:0) + (n>0?n:0));
  if (tot >= sizeof(line)) tot = sizeof(line) - 1;

  // Uwaga: na razie bez sekcji krytycznej; dodamy ją w ETAP 7.
  for (size_t i = 0; i < tot; i++) {
    s_rb[(s_w + i) % s_rb_sz] = (i < (size_t)hn ? head[i] : line[i - (size_t)hn]);
  }
  s_w = (s_w + tot) % s_rb_sz;
}
#endif

void log_write(log_level_t lvl, const char* tag, const char* fmt, ...)
{
    if ((int)lvl > s_threshold) return;

    // 1) Złap argumenty
    va_list ap;
    va_start(ap, fmt);

    // 2) Opcjonalnie wstaw do ring-bufora (zachowaj ap)
#if CONFIG_INFRA_LOG_RINGBUF
    va_list ap2;
    va_copy(ap2, ap);
    rb_push(tag, lvl, fmt, ap2);
    va_end(ap2);
#endif

    // 3) Preformatuj wiadomość do bufora (aby użyć LOG_FORMAT z "%s")
    char msg[192];
    (void)vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    // 4) Wywołaj esp_log_write z LOG_FORMAT(letter, "%s")
    //    UWAGA: LOG_FORMAT oczekuje: (timestamp_u32, tag, ...twoje argumenty...)
    uint32_t ts = esp_log_timestamp();
    const char* tag_str = tag ? tag : "";

    switch (lvl) {
    case LOG_ERROR:
        esp_log_write(ESP_LOG_ERROR,   tag, LOG_FORMAT(E, "%s"), ts, tag_str, msg);
        break;
    case LOG_WARN:
        esp_log_write(ESP_LOG_WARN,    tag, LOG_FORMAT(W, "%s"), ts, tag_str, msg);
        break;
    case LOG_INFO:
        esp_log_write(ESP_LOG_INFO,    tag, LOG_FORMAT(I, "%s"), ts, tag_str, msg);
        break;
    case LOG_DEBUG:
        esp_log_write(ESP_LOG_DEBUG,   tag, LOG_FORMAT(D, "%s"), ts, tag_str, msg);
        break;
    default:
        esp_log_write(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, "%s"), ts, tag_str, msg);
        break;
    }
}
