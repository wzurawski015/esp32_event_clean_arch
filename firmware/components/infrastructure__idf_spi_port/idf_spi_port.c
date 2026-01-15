#include "ports/spi_port.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "SPI_PORT";

struct spi_bus {
    spi_host_device_t host;
    bool dma_enabled;
};

struct spi_device {
    spi_device_handle_t hdev;
    struct spi_bus* bus; // referencja do rodzica
};

port_err_t spi_bus_create(const spi_bus_cfg_t* cfg, spi_bus_handle_t* out_bus)
{
    if (!cfg || !out_bus) return PORT_ERR_INVALID_ARG;

    struct spi_bus* b = calloc(1, sizeof(struct spi_bus));
    if (!b) return PORT_FAIL;

    // Mapowanie ID hosta (uproszczone: 0->auto/SPI2, etc.)
    // Na ESP32-C3/C6 zwykle używa się SPI2_HOST (0)
    b->host = (spi_host_device_t)(cfg->host_id); 
    b->dma_enabled = cfg->enable_dma;

    spi_bus_config_t buscfg = {
        .miso_io_num = cfg->miso_io,
        .mosi_io_num = cfg->mosi_io,
        .sclk_io_num = cfg->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = cfg->max_transfer_sz > 0 ? cfg->max_transfer_sz : 4096,
    };

    // Inicjalizacja magistrali
    // SPI_DMA_CH_AUTO wybiera wolny kanał DMA
    esp_err_t err = spi_bus_initialize(b->host, &buscfg, 
                                       cfg->enable_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bus: %s", esp_err_to_name(err));
        free(b);
        return PORT_FAIL;
    }

    *out_bus = b;
    ESP_LOGI(TAG, "SPI bus initialized (Host=%d, DMA=%d, MaxTx=%d)", 
             (int)b->host, (int)cfg->enable_dma, buscfg.max_transfer_sz);
    return PORT_OK;
}

port_err_t spi_bus_delete(spi_bus_handle_t bus)
{
    if (!bus) return PORT_OK;
    spi_bus_free(bus->host);
    free(bus);
    return PORT_OK;
}

port_err_t spi_dev_add(spi_bus_handle_t bus, const spi_device_cfg_t* cfg, spi_device_handle_t* out_dev)
{
    if (!bus || !cfg || !out_dev) return PORT_ERR_INVALID_ARG;

    struct spi_device* d = calloc(1, sizeof(struct spi_device));
    if (!d) return PORT_FAIL;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->clock_speed_hz,
        .mode           = cfg->mode,
        .spics_io_num   = cfg->cs_io,
        .queue_size     = (cfg->queue_size > 0) ? cfg->queue_size : 1,
        // Domyślne flagi (można rozbudować o cfg)
        // .flags = SPI_DEVICE_HALFDUPLEX, // opcjonalnie
    };

    esp_err_t err = spi_bus_add_device(bus->host, &devcfg, &d->hdev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device CS=%d: %s", cfg->cs_io, esp_err_to_name(err));
        free(d);
        return PORT_FAIL;
    }

    d->bus = bus;
    *out_dev = d;
    ESP_LOGI(TAG, "SPI device added (CS=%d, Freq=%lu Hz, Mode=%d)", 
             cfg->cs_io, (unsigned long)cfg->clock_speed_hz, cfg->mode);
    return PORT_OK;
}

port_err_t spi_dev_remove(spi_device_handle_t dev)
{
    if (!dev) return PORT_OK;
    spi_bus_remove_device(dev->hdev);
    free(dev);
    return PORT_OK;
}

port_err_t spi_transfer(spi_device_handle_t dev, const uint8_t* tx, uint8_t* rx, size_t len)
{
    if (!dev) return PORT_ERR_INVALID_ARG;
    if (len == 0) return PORT_OK;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; // Bity!
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    
    // Używamy polling dla bardzo krótkich transmisji (optymalizacja),
    // a przerwań/DMA dla dłuższych.
    // Tutaj dla uproszczenia zawsze transmit:
    esp_err_t err = spi_device_transmit(dev->hdev, &t);
    
    return (err == ESP_OK) ? PORT_OK : PORT_FAIL;
}
