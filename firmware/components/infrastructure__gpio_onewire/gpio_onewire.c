#include "ports/onewire_port.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* --- Timingi 1-Wire (mikrosekundy) --- */
#define OW_RESET_DUR_US       480
#define OW_PRESENCE_WAIT_US   70
#define OW_SLOT_DUR_US        65
#define OW_READ_SAMPLE_US     10

/* RMT: 1 tick = 1 us (1 MHz) */
#define RMT_RESOLUTION_HZ     1000000

/*
 * PERFEKCYJNA STRUKTURA SPRZĘTOWA RMT (ESP32-C6)
 * Bezpośrednie mapowanie na rejestry sprzętowe.
 */
typedef union {
    struct {
        uint32_t div0   : 15; /*!< Czas trwania 0 */
        uint32_t level0 : 1;  /*!< Poziom 0 */
        uint32_t div1   : 15; /*!< Czas trwania 1 */
        uint32_t level1 : 1;  /*!< Poziom 1 */
    };
    uint32_t val;
} ow_rmt_symbol_t;

struct onewire_bus {
    gpio_num_t           pin;
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t copy_encoder;
};

/* --- Helpers --- */
static inline int _read_gpio(onewire_bus_handle_t bus) {
    return gpio_get_level(bus->pin);
}

/* --- API --- */

port_err_t onewire_bus_create(int gpio_num, onewire_bus_handle_t* out)
{
    if (!out || gpio_num < 0) return PORT_ERR_INVALID_ARG;

    struct onewire_bus* b = calloc(1, sizeof(struct onewire_bus));
    if (!b) return PORT_FAIL;
    b->pin = (gpio_num_t)gpio_num;

    // 1. Konfiguracja kanału RMT TX
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

    // 2. Enkoder Copy
    rmt_copy_encoder_config_t cpy_conf = {};
    if (rmt_new_copy_encoder(&cpy_conf, &b->copy_encoder) != ESP_OK) {
        rmt_del_channel(b->tx_chan);
        free(b);
        return PORT_FAIL;
    }

    // 3. Włączenie kanału
    rmt_enable(b->tx_chan);

    // 4. FIX: Manualne wymuszenie Open-Drain i stanu High
    // Najpierw wymuszamy logiczną jedynkę (żeby w trybie OD tranzystor puścił linię)
    gpio_set_level(b->pin, 1);
    
    // Potem ustawiamy tryb Open-Drain
    gpio_set_direction(b->pin, GPIO_MODE_INPUT_OUTPUT_OD);
    
    // Włączamy Pull-up (wewnętrzny jest słaby, ale lepszy niż nic dla testu bez czujnika)
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
        gpio_reset_pin(bus->pin);
        free(bus);
    }
}

bool onewire_reset(onewire_bus_handle_t bus)
{
    if (!bus) return false;

    // 0. BUS CHECK: Sprawdź, czy linia nie jest zwarta do masy PRZED rozpoczęciem.
    // Jeśli bez czujnika odczytujesz 0, tutaj funkcja zwróci false (i o to chodzi!).
    if (_read_gpio(bus) == 0) {
        return false; // Error: Bus stuck low
    }

    // 1. Symbol RESET: Low 480us, potem High 1us
    ow_rmt_symbol_t sym;
    sym.div0 = 480; sym.level0 = 0;
    sym.div1 = 1;   sym.level1 = 1;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    
    ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, &sym, sizeof(sym), &tx_conf));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));

    // 2. Presence Detect Sequence
    esp_rom_delay_us(OW_PRESENCE_WAIT_US);
    
    int presence = !_read_gpio(bus);

    // 3. Recovery
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
            // Write 1: Low 6us, High 64us
            symbols[i].level0 = 0; symbols[i].div0 = 6;
            symbols[i].level1 = 1; symbols[i].div1 = 64;
        } else {
            // Write 0: Low 60us, High 10us
            symbols[i].level0 = 0; symbols[i].div0 = 60;
            symbols[i].level1 = 1; symbols[i].div1 = 10;
        }
    }

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, symbols, sizeof(symbols), &tx_conf));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));
}

uint8_t onewire_read_byte(onewire_bus_handle_t bus)
{
    if (!bus) return 0;
    uint8_t v = 0;

    // Strobe: Low 2us, High 5us
    ow_rmt_symbol_t strobe;
    strobe.level0 = 0; strobe.div0 = 2;
    strobe.level1 = 1; strobe.div1 = 5;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };

    for (int i = 0; i < 8; i++) {
        // 1. FIRE (RMT)
        ESP_ERROR_CHECK(rmt_transmit(bus->tx_chan, bus->copy_encoder, &strobe, sizeof(strobe), &tx_conf));
        
        // 2. WAIT (Manual delay for precision)
        esp_rom_delay_us(OW_READ_SAMPLE_US);

        // 3. MEASURE
        if (_read_gpio(bus)) {
            v |= (1 << i);
        }

        // 4. CLEANUP & RECOVERY
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(bus->tx_chan, -1));
        esp_rom_delay_us(OW_SLOT_DUR_US - OW_READ_SAMPLE_US - 2);
    }

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

