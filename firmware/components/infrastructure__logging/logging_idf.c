/**
 * @file logging_idf.c
 * @brief Implementacja portu logowania dla ESP-IDF z opcjonalnym ring-bufferem w RAM.
 *
 * @details
 *  - Główna funkcja: ::log_write() – pojedyncze formatowanie i emisja do ESP log + (opcjonalnie) do ring-bufora.
 *  - Ring-buffer:
 *      - stałorozmiarowy, circular, chroniony krótką sekcją krytyczną (portMUX),
 *      - przechowuje **pełne linie** z nagłówkiem „(ts) tag: ...\n”,
 *      - publiczne API: \ref infra_log_rb_stat, \ref infra_log_rb_clear, \ref infra_log_rb_snapshot, \ref infra_log_rb_tail.
 *
 * @par Konfiguracja (menuconfig)
 *  - Components → Infrastructure logging:
 *      - Globalny poziom logowania (ERROR..VERBOSE),
 *      - Włącz ring-buffer loggera,
 *      - Rozmiar ring-buffer (KB),
 *      - (opcjonalnie) CLI `logrb` – zob. logging_cli.c.
 *
 * @dot
 * digraph G {
 *   rankdir=LR; node [shape=box, fontsize=10];
 *   App   -> "ports/log_port.h\n(LOGI/LOGW/...)" -> "log_write()";
 *   "log_write()" -> "ESP-IDF logger\n(esp_log_write)";
 *   "log_write()" -> "Ring buffer\n(RAM)" [label="opcjonalnie"];
 * }
 * @enddot
 */

#include "ports/log_port.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>

static int s_threshold = 4; /**< 0..4: ERROR..VERBOSE */

/** @brief Mapa Kconfig → poziom liczbą (niższa = „bardziej krytycznie”). */
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

/** @brief Ustal próg podczas startu komponentu. */
__attribute__((constructor))
static void _init_log_threshold(void) { s_threshold = cfg2level(); }

#if CONFIG_INFRA_LOG_RINGBUF
/* ===============================  RING-BUFFER  =============================== */

#include <stdlib.h>
#include <string.h>
#include "infra_log_rb.h"      /* publiczne API – nagłówek dodasz w pkt 4a */
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/** @brief Pamięć bufora (circular), rozmiar, indeks zapisu, długość i flaga overflow. */
static char*  s_rb       = NULL;
static size_t s_rb_sz    = 0;      /* pojemność (B) */
static size_t s_w        = 0;      /* indeks zapisu [0..s_rb_sz-1] */
static size_t s_len      = 0;      /* bieżąca ilość ważnych bajtów (<= s_rb_sz) */
static bool   s_overflow = false;  /* czy kiedykolwiek nadpisaliśmy najstarsze */

/** @brief Krótka sekcja krytyczna – wystarcza portMUX (spinlock). */
static portMUX_TYPE s_rb_lock = portMUX_INITIALIZER_UNLOCKED;

/** @brief Alokacja pamięci ring‑bufora. */
__attribute__((constructor))
static void _init_rb(void) {
  s_rb_sz = (size_t)CONFIG_INFRA_LOG_RINGBUF_KB * 1024u;
  if (s_rb_sz < 1024u) s_rb_sz = 1024u;  /* sanity */
  s_rb = (char*)malloc(s_rb_sz);
  s_w = 0;
  s_len = 0;
  s_overflow = false;
}

/** @brief Zapis pojedynczego bajtu do ringu (wewnątrz sekcji krytycznej). */
static inline void rb_write_byte(char b) {
  s_rb[s_w] = b;
  s_w = (s_w + 1) % s_rb_sz;
  if (s_len < s_rb_sz) {
    s_len++;
  } else {
    s_overflow = true; /* nadpisujemy najstarsze */
  }
}

/**
 * @brief Zapis sformatowanej linii: "(timestamp_ms) tag: msg...\n".
 * @param tag  Nazwa tagu (może być NULL → pusty).
 * @param lvl  Poziom logowania (nieużywany do bufora; filtr na wejściu).
 * @param msg  Treść linii (bez znaku nowej linii).
 * @param msg_len Długość treści.
 *
 * @note Zapis jest krótki (tylko kopiowanie bajtów). Nagłówek jest lekki.
 */
static void rb_push_line(const char* tag, log_level_t lvl, const char* msg, size_t msg_len) {
  (void)lvl;
  if (!s_rb || s_rb_sz < 32u || !msg) return;

  char   head[64];
  size_t head_len = 0;

  {
    int hn = snprintf(head, sizeof(head), "(%u) %s: ",
                      (unsigned)esp_log_timestamp(), tag ? tag : "");
    if (hn < 0) hn = 0;
    head_len = (size_t)hn;
  }

  portENTER_CRITICAL(&s_rb_lock);

  /* nagłówek */
  for (size_t i = 0; i < head_len; ++i) rb_write_byte(head[i]);
  /* treść */
  for (size_t i = 0; i < msg_len;  ++i) rb_write_byte(msg[i]);
  /* newline */
  rb_write_byte('\n');

  portEXIT_CRITICAL(&s_rb_lock);
}

/* ===== Publiczne API ring‑bufora – implementacje (patrz: infra_log_rb.h) ===== */

void infra_log_rb_stat(size_t* capacity, size_t* used, bool* overflowed)
{
  size_t cap, len; bool ov;
  portENTER_CRITICAL(&s_rb_lock);
  cap = s_rb_sz; len = s_len; ov = s_overflow;
  portEXIT_CRITICAL(&s_rb_lock);
  if (capacity)   *capacity   = cap;
  if (used)       *used       = len;
  if (overflowed) *overflowed = ov;
}

void infra_log_rb_clear(void)
{
  portENTER_CRITICAL(&s_rb_lock);
  s_w = 0; s_len = 0; s_overflow = false;
  portEXIT_CRITICAL(&s_rb_lock);
}

/** @brief Pomocnicze kopiowanie n bajtów ringu od „start” z zawinięciem. */
static bool rb_copy_from_start(size_t start, char* out, size_t n)
{
  if (!out || !s_rb || n == 0) return false;
  const size_t first = (n < (s_rb_sz - start)) ? n : (s_rb_sz - start);
  memcpy(out, s_rb + start, first);
  if (n > first) memcpy(out + first, s_rb, n - first);
  return true;
}

bool infra_log_rb_snapshot(char* out, size_t max, size_t* out_len)
{
  if (!s_rb || !out || max == 0) return false;

  size_t len, start; bool ok;
  portENTER_CRITICAL(&s_rb_lock);
  len   = s_len;
  start = (s_w + s_rb_sz - s_len) % s_rb_sz; /* najstarszy */
  if (len > max) len = max;
  ok = rb_copy_from_start(start, out, len);
  portEXIT_CRITICAL(&s_rb_lock);

  if (out_len) *out_len = ok ? len : 0;
  return ok;
}

bool infra_log_rb_tail(char* out, size_t max, size_t tail_bytes, size_t* out_len)
{
  if (!s_rb || !out || max == 0) return false;

  size_t len, take, start; bool ok;
  portENTER_CRITICAL(&s_rb_lock);
  len  = s_len;
  take = (tail_bytes < len) ? tail_bytes : len;
  if (take > max) take = max;
  start = (s_w + s_rb_sz - take) % s_rb_sz; /* bierzemy końcówkę */
  ok = rb_copy_from_start(start, out, take);
  portEXIT_CRITICAL(&s_rb_lock);

  if (out_len) *out_len = ok ? take : 0;
  return ok;
}
#endif /* CONFIG_INFRA_LOG_RINGBUF */

/* ================================  LOG WRITE  ================================ */

/**
 * @brief Główny punkt logowania z progiem i poprawnym LOG_FORMAT(...).
 *
 * @param lvl  Poziom (ERROR..VERBOSE).
 * @param tag  Tag ESP-IDF (np. "APP").
 * @param fmt  Format printf‑owy (może być NULL → pusty string).
 * @param ...  Argumenty do @p fmt.
 *
 * @note
 *  - Wykonujemy **jedno** formatowanie do lokalnego bufora i przekazujemy
 *    je do `esp_log_write()` (przez `LOG_FORMAT(letter, "%s")`), co jest
 *    szybsze i bezpieczne.
 *  - Jeśli włączony ring‑buffer, identyczną treść (z nagłówkiem) zapisujemy w RAM.
 */
void log_write(log_level_t lvl, const char* tag, const char* fmt, ...)
{
    if ((int)lvl > s_threshold) return;

    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    int nn = vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);
    if (nn < 0) msg[0] = '\0';

#if CONFIG_INFRA_LOG_RINGBUF
    rb_push_line(tag, lvl, msg, (size_t)strnlen(msg, sizeof(msg)));
#endif

    /* Uwaga: LOG_FORMAT(letter, fmt) rozwinie się do:
       "<kolor>#letter (ts) %s: " fmt "\n", więc musimy podać: (ts, tag, msg) */
    uint32_t ts = esp_log_timestamp();
    const char* t = tag ? tag : "";

    switch (lvl) {
    case LOG_ERROR:
        esp_log_write(ESP_LOG_ERROR,   tag, LOG_FORMAT(E, "%s"), ts, t, msg);
        break;
    case LOG_WARN:
        esp_log_write(ESP_LOG_WARN,    tag, LOG_FORMAT(W, "%s"), ts, t, msg);
        break;
    case LOG_INFO:
        esp_log_write(ESP_LOG_INFO,    tag, LOG_FORMAT(I, "%s"), ts, t, msg);
        break;
    case LOG_DEBUG:
        esp_log_write(ESP_LOG_DEBUG,   tag, LOG_FORMAT(D, "%s"), ts, t, msg);
        break;
    default:
        esp_log_write(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, "%s"), ts, t, msg);
        break;
    }
}
