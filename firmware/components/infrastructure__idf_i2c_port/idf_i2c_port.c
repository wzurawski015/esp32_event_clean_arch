/**
 * @file idf_i2c_port.c
 * @brief Adapter warstwy „infrastructure” dla abstrakcyjnego ports::i2c_port,
 *        oparty o nowy sterownik ESP‑IDF (driver/i2c_master.h).
 *
 *  - Nie konfigurujemy GPIO ręcznie (robi to driver I²C),
 *  - Wewnętrzne podciągi przez pole flags.enable_internal_pullup,
 *  - Każde urządzenie ma ustawioną prędkość SCL (Hz).
 */
#include "idf_i2c_port.h"
#include "ports/i2c_port.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>

typedef struct i2c_bus {
    i2c_master_bus_handle_t hbus;
    uint32_t                clk_hz;
} i2c_bus_t;

typedef struct i2c_dev {
    i2c_master_dev_handle_t hdev;
} i2c_dev_t;

static const char* TAG = "I2C_PORT";

esp_err_t i2c_bus_create(const i2c_bus_cfg_t* cfg, i2c_bus_t** out_bus)
{
    if (!cfg || !out_bus) return ESP_ERR_INVALID_ARG;

    i2c_bus_t* bus = (i2c_bus_t*)calloc(1, sizeof(i2c_bus_t));
    if (!bus) return ESP_ERR_NO_MEM;

    i2c_master_bus_config_t bcfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = 0,  /* większość targetów używa portu 0 */
        .sda_io_num        = cfg->sda_gpio,
        .scl_io_num        = cfg->scl_gpio,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
    };
    bcfg.flags.enable_internal_pullup = cfg->enable_internal_pullup ? 1 : 0;

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bcfg, &bus->hbus),
                        TAG, "i2c_new_master_bus failed");

    bus->clk_hz = cfg->clk_hz ? cfg->clk_hz : 100000;

    *out_bus = bus;
    ESP_LOGI(TAG, "I2C bus created SDA=%d SCL=%d, clk=%u Hz, pullup=%s",
             cfg->sda_gpio, cfg->scl_gpio, (unsigned)bus->clk_hz,
             (cfg->enable_internal_pullup ? "INTEN" : "EXT/none"));
    return ESP_OK;
}

esp_err_t i2c_bus_delete(i2c_bus_t* bus)
{
    if (!bus) return ESP_OK;
    esp_err_t err = i2c_del_master_bus(bus->hbus);
    free(bus);
    return err;
}

esp_err_t i2c_dev_add(i2c_bus_t* bus, uint8_t addr7, i2c_dev_t** out_dev)
{
    if (!bus || !out_dev) return ESP_ERR_INVALID_ARG;

    i2c_dev_t* dev = (i2c_dev_t*)calloc(1, sizeof(i2c_dev_t));
    if (!dev) return ESP_ERR_NO_MEM;

    const i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr7,
        .scl_speed_hz    = bus->clk_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus->hbus, &dcfg, &dev->hdev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device(0x%02X) failed: %s", addr7, esp_err_to_name(err));
        free(dev);
        return err;
    }
    ESP_LOGI(TAG, "device added @ 0x%02X, %u Hz", addr7, (unsigned)bus->clk_hz);

    *out_dev = dev;
    return ESP_OK;
}

esp_err_t i2c_dev_remove(i2c_dev_t* dev)
{
    if (!dev) return ESP_OK;
    esp_err_t err = i2c_master_bus_rm_device(dev->hdev);
    free(dev);
    return err;
}

esp_err_t i2c_tx(i2c_dev_t* dev, const uint8_t* tx, size_t txlen, uint32_t timeout_ms)
{
    if (!dev || (txlen && !tx)) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit(dev->hdev, tx, txlen, timeout_ms);
}

esp_err_t i2c_rx(i2c_dev_t* dev, uint8_t* rx, size_t rxlen, uint32_t timeout_ms)
{
    if (!dev || (rxlen && !rx)) return ESP_ERR_INVALID_ARG;
    return i2c_master_receive(dev->hdev, rx, rxlen, timeout_ms);
}

esp_err_t i2c_txrx(i2c_dev_t* dev,
                   const uint8_t* tx, size_t txlen,
                   uint8_t* rx, size_t rxlen,
                   uint32_t timeout_ms)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev->hdev, tx, txlen, rx, rxlen, timeout_ms);
}

esp_err_t i2c_bus_probe_addr(i2c_bus_t* bus, uint8_t addr7,
                             uint32_t timeout_ms, bool* out_ack)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (out_ack) *out_ack = false;
    if (addr7 < 0x03 || addr7 > 0x77) return ESP_ERR_INVALID_ARG;

    /* i2c_master_probe jest częścią nowego API */
    esp_err_t err = i2c_master_probe(bus->hbus, addr7, timeout_ms);
    if (err == ESP_OK) {
        if (out_ack) *out_ack = true;
        return ESP_OK;
    }
    /* Brak ACK traktujemy jako „nie znaleziono”, ale bez błędu krytycznego */
    if (err == ESP_ERR_TIMEOUT || err == ESP_FAIL) {
        if (out_ack) *out_ack = false;
        return ESP_OK;
    }
    return err; /* param/other */
}

esp_err_t i2c_bus_scan_range(i2c_bus_t* bus,
                             uint8_t start, uint8_t end,
                             uint32_t timeout_ms,
                             uint8_t* out_found, size_t cap, size_t* out_n)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    size_t n = 0;
    for (uint8_t a = start; a <= end; ++a) {
        bool ack = false;
        esp_err_t e = i2c_bus_probe_addr(bus, a, timeout_ms, &ack);
        if (e != ESP_OK) return e;
        if (ack && out_found && n < cap) out_found[n++] = a;
    }
    if (out_n) *out_n = n;
    return ESP_OK;
}
