#include "ports/onewire_port.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
#include "esp_pm.h"            // Power Management
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* --- Timingi 1-Wire (mikrosekundy) --- */
#define OW_RESET_DUR_US       480
#define OW_PRESENCE_WAIT_US   70
#define OW_SLOT_DUR_US        65
#define OW_READ_SAMPLE_US     10

/* RMT Resolution: 1MHz (1 tick = 1 us) */
#define RMT_RESOLUTION_HZ     1000000

/*
 * PERFEKCYJNA STRUKTURA SPRZĘTOWA RMT (ESP32-C6)
 */
typedef union {
    struct {
        uint32_t div0   : 15;
        uint32_t level0 : 1; 
        uint32_t div1   : 15;
        uint32_t level1 : 1; 
    };
    uint32_t val;
} ow_rmt_symbol_t;

struct onewire_bus {
    gpio_num_t           pin;
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t copy_encoder;
#if CONFIG_PM_ENABLE
    esp_pm_lock_handle_t pm_lock; // Blokada zegara
#endif
};

/* --- Helpers --- */
static inline int _read_gpio(onewire_bus_handle_t bus) {
    return gpio_get_level(bus->pin);
}

static inline void _pm_lock_acquire(onewire_bus_handle_t bus) {
#if CONFIG_PM_ENABLE
    if (bus->pm_lock) esp_pm_lock_acquire(bus->pm_lock);
#endif
}

static inline void _pm_lock_release(onewire_bus_handle_t bus) {
#if CONFIG_PM_ENABLE
    if (bus->pm_lock) esp_pm_lock_release(bus->pm_lock);
#endif
}

/* --- API --- */

port_err_t onewire_bus_create(int gpio_num, onewire_bus_handle_t* out)
{
    if (!out || gpio_num < 0) return PORT_ERR_INVALID_ARG;

    struct onewire_bus* b = calloc(1, sizeof(struct onewire_bus));
    if (!b) return PORT_FAIL;
    b->pin = (gpio_num_t)gpio_num;

#if CONFIG_PM_ENABLE
    // Blokada zabraniająca obniżania zegara APB poniżej MAX podczas transmisji
    esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "ow_rmt", &b->pm_lock);
#endif

    // Konfiguracja RMT TX
    rmt_tx_channel_config_t tx_conf = {
        .gpio_num = b->pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64, 
        .trans_queue_depth = 4,
        .flags.with_dma = 0,
    };

    if (rmt_new_tx_channel(&tx_conf, &b->tx_chan) != ESP_OK) {
        free(b);
        return PORT_FAIL;
    }

    rmt_copy_encoder_config_t cpy_conf = {};
    if (rmt_new_copy_encoder(&cpy_conf, &b->copy_encoder) != ESP_OK) {
        rmt_del_channel(b->tx_chan);
        free(b);
        return PORT_FAIL;
    }

    rmt_enable(b->tx_chan);

    // Manualne wymuszenie Open-Drain (Critical Fix dla C6)
    gpio_set_level(b->pin, 1);
    gpio_set_direction(b->pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(b->pin, GPIO_PULLUP_ENABLE);

    *out = b;
    return PORT_OK;
}

void onewire_bus_delete(onewire_bus_handle_t bus)
{
    if (bus) {
        if (bus->tx_chan) {
            rmt_disable(bus->tx_chan);
            rmt_del_channel(bus->tx_chan);
        }
        if (bus->copy_encoder) {
            rmt_del_encoder(bus->copy_encoder);
        }
#if CONFIG_PM_ENABLE
        if (bus->pm_lock) {
            esp_pm_lock_delete(bus->pm_lock);
        }
#endif
        gpio_reset_pin(bus->pin);
        free(bus);
    }
}

bool onewire_reset(onewire_bus_handle_t bus)
{
    if (!bus) return false;

    // Bus Check
    if (_read_gpio(bus) == 0) return false;

    _pm_lock_acquire(bus);

    ow_rmt_symbol_t sym;
    sym.div0 = 480; sym.level0 = 0;
    sym.div1 = 1;   sym.level1 = 1;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, &sym, sizeof(sym), &tx_conf));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));

    _pm_lock_release(bus);

    esp_rom_delay_us(OW_PRESENCE_WAIT_US);
    int presence = !_read_gpio(bus);
    esp_rom_delay_us(OW_RESET_DUR_US - OW_PRESENCE_WAIT_US);

    return (bool)presence;
}

void onewire_write_byte(onewire_bus_handle_t bus, uint8_t v)
{
    if (!bus) return;

    ow_rmt_symbol_t symbols[8];
    memset(symbols, 0, sizeof(symbols));

    for (int i = 0; i < 8; i++) {
        if (v & (1 << i)) {
            symbols[i].level0 = 0; symbols[i].div0 = 6;
            symbols[i].level1 = 1; symbols[i].div1 = 64;
        } else {
            symbols[i].level0 = 0; symbols[i].div0 = 60;
            symbols[i].level1 = 1; symbols[i].div1 = 10;
        }
    }

    _pm_lock_acquire(bus);
    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, symbols, sizeof(symbols), &tx_conf));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));
    _pm_lock_release(bus);
}

uint8_t onewire_read_byte(onewire_bus_handle_t bus)
{
    if (!bus) return 0;
    uint8_t v = 0;

    ow_rmt_symbol_t strobe;
    strobe.level0 = 0; strobe.div0 = 2;
    strobe.level1 = 1; strobe.div1 = 5;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };

    _pm_lock_acquire(bus); // Trzymamy lock przez cały bajt

    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, &strobe, sizeof(strobe), &tx_conf));
        
        esp_rom_delay_us(OW_READ_SAMPLE_US);
        if (_read_gpio(bus)) v |= (1 << i);

        ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));
        esp_rom_delay_us(OW_SLOT_DUR_US - OW_READ_SAMPLE_US - 2);
    }

    _pm_lock_release(bus);
    return v;
}

uint8_t onewire_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    while (len--) {
        uint8_t inbyte = *data++;
        for (uint8_t i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

