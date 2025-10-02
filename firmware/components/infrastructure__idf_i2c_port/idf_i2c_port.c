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
        .intr_priority     = 0,  /* domyślnie */
    };
    bcfg.flags.enable_internal_pullup = cfg->enable_internal_pullup ? 1 : 0;

    i2c_master_bus_handle_t hbus = NULL;
    esp_err_t err = i2c_new_master_bus(&bcfg, &hbus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus() failed: %s", esp_err_to_name(err));
        free(bus);
        return err;
    }

    bus->hbus   = hbus;
    bus->clk_hz = (cfg->clk_hz != 0) ? cfg->clk_hz : 100000; /* domyślnie 100 kHz */

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
