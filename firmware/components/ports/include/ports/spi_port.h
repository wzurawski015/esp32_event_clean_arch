#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ports/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque types (ukrywające implementację IDF) */
typedef struct spi_bus spi_bus_t;
typedef struct spi_dev spi_dev_t;

/** Konfiguracja magistrali SPI */
typedef struct {
    int mosi_io;
    int miso_io;
    int sclk_io;
    int max_transfer_sz; 
    bool enable_dma;
    int host_id; 
} spi_bus_cfg_t;

/** Konfiguracja urządzenia SPI (Slave) */
typedef struct {
    int cs_io;              
    uint32_t clock_speed_hz; 
    uint8_t mode;           
    uint8_t queue_size;     
} spi_device_cfg_t;

/* --- Zarządzanie magistralą --- */
port_err_t spi_bus_create(const spi_bus_cfg_t* cfg, spi_bus_t** out_bus);
port_err_t spi_bus_delete(spi_bus_t* bus);

/* --- Zarządzanie urządzeniami --- */
port_err_t spi_dev_add(spi_bus_t* bus, const spi_device_cfg_t* cfg, spi_dev_t** out_dev);
port_err_t spi_dev_remove(spi_dev_t* dev);

/* --- Transfer (Full Duplex) --- */
port_err_t spi_transfer(spi_dev_t* dev, const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
