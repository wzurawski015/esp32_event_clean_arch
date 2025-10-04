/**
 * @file infra_log_rb.h
 * @brief Publiczne API do snapshot/tail/stat/clear ring-bufora logów.
 *
 * @dot
 * digraph RB {
 *   rankdir=LR; node [shape=box, fontsize=10];
 *   "log_write()" -> "Ring buffer" [label="rb_push_line"];
 *   "CLI logrb"   -> "Ring buffer" [label="stat/tail/dump/clear"];
 * }
 * @enddot
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Zwraca pojemność bufora, bieżące użycie oraz flagę overflow. */
void infra_log_rb_stat(size_t* capacity, size_t* used, bool* overflowed);

/** @brief Czyści całą zawartość ringu (atomowo). */
void infra_log_rb_clear(void);

/**
 * @brief Wykonuje snapshot całej zawartości (od najstarszych do najnowszych).
 *
 * @param[out] out     Bufor wyjściowy.
 * @param[in]  max     Rozmiar bufora @p out.
 * @param[out] out_len Faktyczna liczba skopiowanych bajtów.
 * @return true, jeśli skopiowano; false w razie błędu parametrów.
 */
bool infra_log_rb_snapshot(char* out, size_t max, size_t* out_len);

/**
 * @brief Zwraca końcówkę logów – ostatnie @p tail_bytes bajtów (lub mniej).
 *
 * @param[out] out     Bufor wyjściowy.
 * @param[in]  max     Rozmiar bufora @p out.
 * @param[in]  tail_bytes Żądana ilość bajtów od końca.
 * @param[out] out_len Faktyczna liczba skopiowanych bajtów.
 * @return true/false (jak wyżej).
 */
bool infra_log_rb_tail(char* out, size_t max, size_t tail_bytes, size_t* out_len);

#ifdef __cplusplus
}
#endif
