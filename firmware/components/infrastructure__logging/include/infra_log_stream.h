#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Log-stream: SPSC ring buffer na bajty (producer->consumer) bez alokacji.
 *
 * Uwaga: rdzeń jest SPSC. Jeżeli potencjalnie masz więcej niż jednego
 * producenta, musisz go serializować zewnętrznie (np. przez mutex/critical).
 */
void infra_log_stream_init(void);

// Producer side
bool infra_log_stream_write_all(const void* data, size_t len);

// Consumer side
const uint8_t* infra_log_stream_peek(size_t* out_len);
void infra_log_stream_consume(size_t len);

// Stats
size_t infra_log_stream_capacity(void);
size_t infra_log_stream_used(void);
uint32_t infra_log_stream_drop_count(void);
