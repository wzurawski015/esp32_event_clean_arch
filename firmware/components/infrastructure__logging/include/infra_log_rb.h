#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Zwraca aktualny *rozmiar logiczny* danych (ile bajtów jest „pełnych” do odczytu). */
size_t infra_log_rb_len(void);

/** Zwraca rozmiar fizyczny bufora (pojemność). */
size_t infra_log_rb_cap(void);

/**
 * Odczyt chronologiczny (od najstarszego do najnowszego).
 *  - offset: przesunięcie od najstarszego bajtu [0..infra_log_rb_len()).
 *  - dst/len: bufor docelowy i jego rozmiar.
 * Zwraca liczbę wypełnionych bajtów.
 * Nigdy nie blokuje; snapshot wskaźników wykonywany jest bezciężko.
 */
size_t infra_log_rb_read(size_t offset, uint8_t *dst, size_t len);

/** Zrzut całości ring-bufora do logów (INFO), w porcjach bez przepełniania. */
void infra_log_rb_dump(void);

/** (Opcj.) Zrzut w trybie HEX (dla danych binarnych). */
void infra_log_rb_dump_hex(size_t bytes_per_line);

#ifdef __cplusplus
}
#endif
