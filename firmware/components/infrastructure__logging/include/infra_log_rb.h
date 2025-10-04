#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// #include "esp_err.h"  // nie jest potrzebne w tym nagłówku, zostaw jeśli używasz gdzie indziej

#ifdef __cplusplus
extern "C" {
#endif

/* ===== API już przez Ciebie dodane (zostawiamy) ===== */

/** Zwraca aktualny rozmiar logiczny danych (ile bajtów jest pełnych do odczytu). */
size_t infra_log_rb_len(void);

/** Zwraca rozmiar fizyczny bufora (pojemność). */
size_t infra_log_rb_cap(void);

/**
 * Odczyt chronologiczny (od najstarszego do najnowszego).
 *  - offset: przesunięcie od najstarszego bajtu [0..infra_log_rb_len()).
 *  - dst/len: bufor docelowy i jego rozmiar.
 * Zwraca liczbę wypełnionych bajtów. Snapshot wskaźników bez ciężkich sekcji krytycznych.
 */
size_t infra_log_rb_read(size_t offset, uint8_t *dst, size_t len);

/** Zrzut całości ring-bufora do logów (INFO), w porcjach bez przepełniania. */
void infra_log_rb_dump(void);

/** (Opcjonalnie) Zrzut w trybie HEX (dla danych binarnych). */
void infra_log_rb_dump_hex(size_t bytes_per_line);


/* ===== API wymagane przez logging_cli.c (brakujące wcześniej) =====
 * Te cztery deklaracje MUSZĄ być widoczne przy włączonym ring‑bufferze.
 * Podpisy są zgodne z użyciem w logging_cli.c.
 */
#if CONFIG_INFRA_LOG_RINGBUF
/** Zwraca pojemność, zajętość oraz flagę overflow (wskaźniki mogą być NULL). */
void infra_log_rb_stat(size_t *out_cap, size_t *out_used, bool *out_overflow);

/** Czyści bufor (zeruje zawartość i flagę overflow). */
void infra_log_rb_clear(void);

/** Migawka najstarsze→najnowsze do dst; true jeśli cokolwiek skopiowano. */
bool infra_log_rb_snapshot(char *dst, size_t dst_cap, size_t *out_got);

/** Zapisuje do dst ostatnie want_tail bajtów; true jeśli cokolwiek skopiowano. */
bool infra_log_rb_tail(char *dst, size_t dst_cap, size_t want_tail, size_t *out_got);
#endif /* CONFIG_INFRA_LOG_RINGBUF */

#ifdef __cplusplus
}
#endif
