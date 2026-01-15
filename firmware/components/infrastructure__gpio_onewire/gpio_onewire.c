#include "ports/onewire_port.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Timingi 1-Wire (us) */
#define OW_RESET_us       480
#define OW_PRESENCE_us    70
#define OW_WRITE_SLOT_us  65
#define OW_WRITE_0_LOW_us 60
#define OW_WRITE_1_LOW_us 6
#define OW_READ_SAMP_us   10
#define OW_REC_us         5

struct onewire_bus {
    gpio_num_t pin;
    portMUX_TYPE mux;
};

static void _low(gpio_num_t p) {
    gpio_set_direction(p, GPIO_MODE_OUTPUT);
    gpio_set_level(p, 0);
}

static void _release(gpio_num_t p) {
    gpio_set_direction(p, GPIO_MODE_INPUT);
}

static int _read(gpio_num_t p) {
    return gpio_get_level(p);
}

port_err_t onewire_bus_create(int gpio_num, onewire_bus_handle_t* out)
{
    if (!out) return PORT_ERR_INVALID_ARG;
    
    struct onewire_bus* b = calloc(1, sizeof(struct onewire_bus));
    if (!b) return PORT_FAIL;

    b->pin = (gpio_num_t)gpio_num;
    portMUX_INITIALIZE(&b->mux);

    // Konfiguracja Open-Drain (symulowana przez Input/Output switch) lub prawdziwe OD
    // Używamy Input/Output switch dla pewności na wszystkich ESP.
    gpio_reset_pin(b->pin);
    _release(b->pin);

    *out = b;
    return PORT_OK;
}

void onewire_bus_delete(onewire_bus_handle_t bus)
{
    if (bus) free(bus);
}

bool onewire_reset(onewire_bus_handle_t bus)
{
    if (!bus) return false;
    // Reset nie wymaga sekcji krytycznej na całość (jest długi), ale wymaga atomowości przy zboczu.
    
    _low(bus->pin);
    esp_rom_delay_us(OW_RESET_us);
    
    portENTER_CRITICAL(&bus->mux);
    _release(bus->pin);
    portEXIT_CRITICAL(&bus->mux);
    
    esp_rom_delay_us(OW_PRESENCE_us);
    int presence = !_read(bus->pin);
    esp_rom_delay_us(OW_RESET_us - OW_PRESENCE_us);
    
    return (bool)presence;
}

static void _write_bit(onewire_bus_handle_t bus, int bit)
{
    portENTER_CRITICAL(&bus->mux);
    _low(bus->pin);
    if (bit) {
        esp_rom_delay_us(OW_WRITE_1_LOW_us);
        _release(bus->pin);
        esp_rom_delay_us(OW_WRITE_SLOT_us - OW_WRITE_1_LOW_us);
    } else {
        esp_rom_delay_us(OW_WRITE_0_LOW_us);
        _release(bus->pin);
        esp_rom_delay_us(OW_WRITE_SLOT_us - OW_WRITE_0_LOW_us);
    }
    portEXIT_CRITICAL(&bus->mux);
    esp_rom_delay_us(OW_REC_us);
}

static int _read_bit(onewire_bus_handle_t bus)
{
    int r;
    portENTER_CRITICAL(&bus->mux);
    _low(bus->pin);
    esp_rom_delay_us(2); // krótki puls startu
    _release(bus->pin);
    esp_rom_delay_us(OW_READ_SAMP_us);
    r = _read(bus->pin);
    portEXIT_CRITICAL(&bus->mux);
    esp_rom_delay_us(OW_WRITE_SLOT_us - OW_READ_SAMP_us);
    return r;
}

void onewire_write_byte(onewire_bus_handle_t bus, uint8_t v)
{
    for (int i=0; i<8; i++) {
        _write_bit(bus, (v >> i) & 1);
    }
}

uint8_t onewire_read_byte(onewire_bus_handle_t bus)
{
    uint8_t v = 0;
    for (int i=0; i<8; i++) {
        if (_read_bit(bus)) v |= (1<<i);
    }
    return v;
}

// CRC8 Maxim/Dallas (X^8 + X^5 + X^4 + 1)
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
