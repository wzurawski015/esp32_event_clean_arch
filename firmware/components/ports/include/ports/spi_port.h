#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ports/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uchwyty nieprzezroczyste (ukrywające implementację IDF) */
typedef struct spi_bus* spi_bus_handle_t;
typedef struct spi_device* spi_device_handle_t;

/** Konfiguracja magistrali SPI */
typedef struct {
    int mosi_io;
    int miso_io;
    int sclk_io;
    /* Maksymalny rozmiar transferu w bajtach. 
     * Ważne dla alokacji wewnętrznych buforów DMA. */
    int max_transfer_sz; 
    /* Czy używać DMA? (true = SPI_DMA_CH_AUTO) */
    bool enable_dma;
    /* ID hosta SPI (np. 1=SPI2_HOST, 2=SPI3_HOST na ESP32) */
    int host_id; 
} spi_bus_cfg_t;

/** Konfiguracja urządzenia SPI (Slave) */
typedef struct {
    int cs_io;              /**< Pin Chip Select */
    uint32_t clock_speed_hz; /**< Np. 10*1000*1000 dla 10MHz */
    uint8_t mode;           /**< 0, 1, 2, 3 (CPOL/CPHA) */
    uint8_t queue_size;     /**< Głębokość kolejki (dla usług asynchronicznych) */
} spi_device_cfg_t;

/* --- Zarządzanie magistralą --- */
port_err_t spi_bus_create(const spi_bus_cfg_t* cfg, spi_bus_handle_t* out_bus);
port_err_t spi_bus_delete(spi_bus_handle_t bus);

/* --- Zarządzanie urządzeniami --- */
port_err_t spi_dev_add(spi_bus_handle_t bus, const spi_device_cfg_t* cfg, spi_device_handle_t* out_dev);
port_err_t spi_dev_remove(spi_device_handle_t dev);

/* --- Transfer (Full Duplex) --- */
/**
 * @brief Wykonuje transakcję SPI (blokującą/polling).
 * * @param dev Uchwyt urządzenia
 * @param tx  Bufor nadawczy (może być NULL)
 * @param rx  Bufor odbiorczy (może być NULL)
 * @param len Długość w bajtach
 * * @note Jeśli włączono DMA, bufory powinny być w pamięci DRAM (lub użyjemy wewnętrznego kopiowania w driverze).
 */
port_err_t spi_transfer(spi_device_handle_t dev, const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
