/**
 * @file services_ds18b20_ev.c
 * @brief Serwis DS18B20 – w pełni **event-driven**, bez blokujących delayów.
 *
 * - 1‑Wire na wybranym GPIO (open‑drain),
 * - uruchamiany okresowym esp_timer (periodic) – co okres wyzwala pomiar,
 * - czas konwersji realizowany przez esp_timer one‑shot (brak vTaskDelay),
 * - przelicza temperaturę (milli‑C) i publikuje EV_DS18_READY,
 * - błędy reportuje EV_DS18_ERROR.
 *
 * @dot
 * digraph SM {
 *   rankdir=LR; node [shape=ellipse, fontname="Helvetica"];
 *   IDLE; KICK; WAIT; READ;
 *   IDLE -> KICK [label="T(periodic)"];
 *   KICK -> WAIT [label="start convert OK"];
 *   KICK -> IDLE [label="start convert FAIL / EV_DS18_ERROR"];
 *   WAIT -> READ [label="T(once = t_conv(bits))"];
 *   READ -> IDLE [label="publish EV_DS18_READY"];
 * }
 * @enddot
 */
#include "services_ds18b20_ev.h"
#include "core_ev.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char* TAG = "DS18_SVC";

/* --- 1-Wire prymitywy (single-wire, open-drain) --- */
static int s_gpio = -1;

static inline void line_low(void) {
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_gpio, 0);
}
static inline void line_release(void) {
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
}
static inline int line_read(void) {
    return gpio_get_level(s_gpio);
}

static bool ow_reset(void) {
    /* reset: low 480us -> release -> po ~70us presence powinien być 0 */
    line_low();
    esp_rom_delay_us(480);
    line_release();
    esp_rom_delay_us(70);
    int presence = (line_read() == 0);
    esp_rom_delay_us(410);
    return presence;
}
static void ow_write_bit(int bit) {
    if (bit) {
        line_low();        esp_rom_delay_us(6);
        line_release();    esp_rom_delay_us(64);
    } else {
        line_low();        esp_rom_delay_us(60);
        line_release();    esp_rom_delay_us(10);
    }
}
static int ow_read_bit(void) {
    int r;
    line_low();         esp_rom_delay_us(3);
    line_release();     esp_rom_delay_us(10);
    r = line_read();    esp_rom_delay_us(53);
    return r;
}
static void ow_write_byte(uint8_t b) {
    for (int i = 0; i < 8; i++) ow_write_bit((b >> i) & 1);
}
static uint8_t ow_read_byte(void) {
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v |= (ow_read_bit() ? 1U : 0U) << i;
    return v;
}

/* --- DS18B20 komendy --- */
#define DS_CMD_SKIP_ROM 0xCC
#define DS_CMD_CONVERTT 0x44
#define DS_CMD_RSCRATCH 0xBE

/* --- Serwisowy automat --- */
typedef enum { S_IDLE, S_KICK_CONVERT, S_WAIT_CONVERT, S_READ } st_t;
static st_t s_st = S_IDLE;

static esp_timer_handle_t s_t_period = NULL;
static esp_timer_handle_t s_t_once   = NULL;

static int s_res_bits   = 12;
static int s_period_ms  = 1000;

static int ms_for_res(int bits) {
    switch (bits) {
        case 9:  return 94;
        case 10: return 188;
        case 11: return 375;
        default: return 750;
    }
}

/* event queue (subskrypcja core__ev) */
static ev_queue_t s_q;

/* forward */
static void ds18_task(void* arg);
static void process_sm(void);

static void timer_once_cb(void* arg) {
    (void)arg;
    /* sygnał wewnętrzny – używamy EV_DS18_READY jako "TIMER_DONE" */
    ev_post(EV_SRC_DS18, EV_DS18_READY, 0, 0);
}
static void timer_period_cb(void* arg) {
    (void)arg;
    if (s_st == S_IDLE) {
        s_st = S_KICK_CONVERT;
        ev_post(EV_SRC_DS18, EV_DS18_READY, 0, 0); /* „tick” do przetworzenia */
    }
}

static void start_once(int ms) {
    if (!s_t_once) {
        const esp_timer_create_args_t a = { .callback = timer_once_cb, .name = "ds18_once" };
        esp_err_t e = esp_timer_create(&a, &s_t_once);
        (void)e;
    }
    esp_timer_stop(s_t_once);
    esp_timer_start_once(s_t_once, (uint64_t)ms * 1000ULL);
}

static bool ds_start_convert(void) {
    if (!ow_reset()) return false;
    ow_write_byte(DS_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_CONVERTT);
    return true;
}

static bool ds_read_temp_raw(int16_t* out_raw) {
    if (!ow_reset()) return false;
    ow_write_byte(DS_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_RSCRATCH);
    uint8_t t_lo = ow_read_byte();
    uint8_t t_hi = ow_read_byte();
    for (int i = 0; i < 7; i++) (void)ow_read_byte(); /* pomijamy CRC i resztę scratchpada */
    *out_raw = (int16_t)((t_hi << 8) | t_lo);
    return true;
}

static void process_sm(void) {
    switch (s_st) {
        case S_IDLE:
            break;

        case S_KICK_CONVERT:
            if (ds_start_convert()) {
                s_st = S_WAIT_CONVERT;
                start_once(ms_for_res(s_res_bits) + 20);
            } else {
                ev_post(EV_SRC_DS18, EV_DS18_ERROR, 1, 0);
                s_st = S_IDLE;
            }
            break;

        case S_WAIT_CONVERT:
            /* wewnętrzny timer_once podbije EV_DS18_READY -> przejście do READ */
            break;

        case S_READ: {
            int16_t raw = 0;
            if (ds_read_temp_raw(&raw)) {
                /* 12-bit: 1 LSB = 0.0625°C, milli‑C: *62.5 => *625/10 */
                int milliC = (int)((raw * 625) / 10);
                ev_post(EV_SRC_DS18, EV_DS18_READY, (uintptr_t)milliC, 0);
            } else {
                ev_post(EV_SRC_DS18, EV_DS18_ERROR, 2, 0);
            }
            s_st = S_IDLE;
        } break;

        default:
            s_st = S_IDLE;
            break;
    }
}

/* Główny task serwisu – czysty event loop. */
static void ds18_task(void* arg) {
    (void)arg;
    ev_msg_t m;
    for (;;) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) == pdTRUE) {
            if (m.src == EV_SRC_DS18 && m.code == EV_DS18_READY) {
                if (s_st == S_WAIT_CONVERT) {
                    s_st = S_READ;
                    process_sm();
                } else if (s_st == S_KICK_CONVERT) {
                    process_sm();
                } else if (s_st == S_IDLE) {
                    /* heartbeat z periodica – rozpoczynamy nowy cykl */
                    s_st = S_KICK_CONVERT;
                    process_sm();
                }
            }
        }
    }
}

/* API serwisu */
bool services_ds18_start(const ds18_svc_cfg_t* cfg) {
    if (!cfg) return false;
    s_gpio      = cfg->gpio;
    s_res_bits  = cfg->resolution_bits;
    s_period_ms = cfg->period_ms;

    /* GPIO open‑drain z (opcjonalnym) pull‑upem */
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_gpio),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = cfg->internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    line_release();

    if (!ev_subscribe(&s_q, 8)) return false;

    /* periodic timer (odpala cały cykl pomiaru) */
    if (!s_t_period) {
        const esp_timer_create_args_t a = { .callback = timer_period_cb, .name = "ds18_period" };
        esp_err_t e = esp_timer_create(&a, &s_t_period);
        (void)e;
    }
    esp_timer_start_periodic(s_t_period, (uint64_t)s_period_ms * 1000ULL);

    /* task eventowy */
    TaskHandle_t th = NULL;
    if (xTaskCreate(ds18_task, "ds18_ev", 4096, NULL, 4, &th) != pdPASS) return false;

    ESP_LOGI(TAG, "DS18B20 service started on GPIO%d, res=%db, period=%dms",
             s_gpio, s_res_bits, s_period_ms);
    return true;
}

void services_ds18_stop(void) {
    if (s_t_period) esp_timer_stop(s_t_period);
    if (s_t_once)   esp_timer_stop(s_t_once);
}
