#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ports/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onewire_bus* onewire_bus_handle_t;

port_err_t onewire_bus_create(int gpio_num, onewire_bus_handle_t* out);
void       onewire_bus_delete(onewire_bus_handle_t bus);
bool       onewire_reset(onewire_bus_handle_t bus);
void       onewire_write_byte(onewire_bus_handle_t bus, uint8_t v);
uint8_t    onewire_read_byte(onewire_bus_handle_t bus);
uint8_t    onewire_crc8(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
