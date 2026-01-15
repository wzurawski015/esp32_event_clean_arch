#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief SPSC ring buffer (Single Producer / Single Consumer) dla strumieni bajtów.
 *
 * Wzorzec użycia:
 *  - producent: reserve() -> zapis -> commit()
 *  - konsument: peek() -> odczyt -> consume()
 *
 * Kontrakt:
 *  - rb->cap musi być potęgą 2 (mask = cap-1),
 *  - reserve()/peek() zwracają wskaźnik do *ciągłego* fragmentu; jeśli
 *    potrzeba więcej bajtów, należy wykonać kolejne reserve()/peek() w pętli.
 *
 * Uwaga:
 *  - implementacja jest lock‑free przy założeniu 1 producent + 1 konsument.
 *    Dla wielu producentów/konsumentów wymagane jest zewnętrzne serializowanie.
 */
typedef struct
{
    uint8_t* buf;
    uint32_t cap;   // bytes (power‑of‑two)
    uint32_t mask;  // cap - 1

    uint32_t head;  // producer writes, consumer reads
    uint32_t tail;  // consumer writes, producer reads
} spsc_ring_t;

/**
 * @brief Inicjalizuje ring.
 * @param rb        obiekt ringa
 * @param storage   bufor danych (cap_bytes bajtów)
 * @param cap_bytes rozmiar bufora w bajtach (musi być potęgą 2)
 */
bool spsc_ring_init(spsc_ring_t* rb, void* storage, uint32_t cap_bytes);

static inline size_t spsc_ring_capacity(const spsc_ring_t* rb)
{
    return rb ? (size_t)rb->cap : 0u;
}

/** @brief Ilość bajtów dostępnych do odczytu (przybliżone, ale spójne dla SPSC). */
size_t spsc_ring_used(const spsc_ring_t* rb);

/** @brief Ilość wolnego miejsca w bajtach. */
size_t spsc_ring_free(const spsc_ring_t* rb);

/**
 * @brief Rezerwuje fragment do zapisu.
 * @param want  ile bajtów chcemy zapisać
 * @param out_n ile bajtów *ciągłych* jest dostępne pod zwróconym wskaźnikiem
 * @return wskaźnik do bufora zapisu albo NULL jeśli ring pełny
 */
uint8_t* spsc_ring_reserve(spsc_ring_t* rb, size_t want, size_t* out_n);

/** @brief Zatwierdza zapisane bajty (n <= out_n z reserve). */
void spsc_ring_commit(spsc_ring_t* rb, size_t n);

/**
 * @brief Podejrzy fragment do odczytu.
 * @param out_n ile bajtów *ciągłych* jest dostępne pod zwróconym wskaźnikiem
 * @return wskaźnik do bufora odczytu albo NULL jeśli ring pusty
 */
const uint8_t* spsc_ring_peek(const spsc_ring_t* rb, size_t* out_n);

/** @brief Zużywa odczytane bajty (n <= out_n z peek). */
void spsc_ring_consume(spsc_ring_t* rb, size_t n);
