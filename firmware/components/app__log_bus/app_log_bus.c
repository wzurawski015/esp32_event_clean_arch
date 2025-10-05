#include "app_log_bus.h"
#include "core_ev.h"
#include "core/leasepool.h"
#include "ports/log_port.h"
#include "esp_log.h"
#include <stdarg.h>
#include <string.h>

static const char* TAG = "APP_LOG_BUS";

// Poprzedni vprintf z IDF / własnej infrastruktury – wołamy go, by nie psuć konsoli i ringa
static int (*s_prev_vprintf)(const char *fmt, va_list) = NULL;

// Bufor akumulujący znaki do końca linii (prosty FIFO ogon)
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
#define ACC_CAP 512
static char   s_acc[ACC_CAP];
static size_t s_acc_len = 0;

static void logbus_flush_line_locked(void)
{
    size_t len = s_acc_len;
    if (len == 0) return;

    // Usuń opcjonalne '\r' na końcu
    if (len > 0 && s_acc[len - 1] == '\r') {
        len--;
    }

    // Alokuj lease – jeśli linia > cap, wysyłamy ogon (ostatnie cap bajtów)
    lp_handle_t h = lp_alloc_try((uint32_t)len);
    if (h.idx != LP_INVALID_IDX) {
        lp_view_t v;
        if (lp_acquire(h, &v)) {
            size_t copy_len = len;
            const char* src = s_acc;
            if (copy_len > v.cap) {
                src      = s_acc + (copy_len - v.cap);
                copy_len = v.cap;
            }
            memcpy(v.ptr, src, copy_len);
            lp_commit(h, (uint32_t)copy_len);
            // Zero-copy broadcast – bus podbije refy i zwolni ref producenta
            ev_post_lease(EV_SRC_LOG, EV_LOG_NEW, h, (uint16_t)copy_len);
        } else {
            // Na wszelki wypadek – usprawnienie bezpieczeństwa
            lp_release(h);
        }
    }
    s_acc_len = 0;
}

static void logbus_append_chunk_locked(const char* chunk, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        char c = chunk[i];
        if (c == '\n') {
            // kończymy linię i publikujemy
            logbus_flush_line_locked();
        } else {
            if (s_acc_len < ACC_CAP) {
                s_acc[s_acc_len++] = c;
            } else {
                // proste "drop-head": przesuwamy o 1, żeby zachować ogon linii
                memmove(s_acc, s_acc + 1, ACC_CAP - 1);
                s_acc[ACC_CAP - 1] = c;
                // s_acc_len pozostaje == ACC_CAP
            }
        }
    }
}

static int logbus_vprintf(const char* fmt, va_list ap)
{
    // 1) przekaż dalej (konsola/klasyczny ring/CLI), nie psujemy istniejącego systemu
    int ret = 0;
    if (s_prev_vprintf) {
        va_list ap_fwd;
        va_copy(ap_fwd, ap);
        ret = s_prev_vprintf(fmt, ap_fwd);
        va_end(ap_fwd);
    }

    // 2) sformatuj do bufora tymczasowego i dołóż do akumulatora linii
    char tmp[256];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n > 0) {
        // Chronimy tylko akumulator (enqueue na busie jest nieblokujące)
        portENTER_CRITICAL(&s_log_mux);
        logbus_append_chunk_locked(tmp, (size_t)n);
        portEXIT_CRITICAL(&s_log_mux);
    }
    return ret;
}

bool app_log_bus_start(void)
{
    // Wrapujemy aktualny vprintf
    s_prev_vprintf = esp_log_set_vprintf(logbus_vprintf);
    LOGI(TAG, "Log-bus ready: vprintf wrapped -> EV_LOG_NEW (LEASE, zero-copy).");
    return true;
}
