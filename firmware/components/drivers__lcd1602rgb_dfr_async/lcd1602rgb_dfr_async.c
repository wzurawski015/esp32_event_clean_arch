/**
 * @file lcd1602rgb_dfr_async.c
 * @brief Sterownik DFRobot LCD1602 RGB (DFR0464 v2.0) – nieblokujący, event-driven.
 *
 *  - LCD: ST7032/AIP31068 @ 0x3E
 *  - RGB: PCA9633-compatible @ 0x2D (część płytek używa 0x62)
 *  - Init dostrojony pod 3.3 V (ICON=1, BOOST=1, kontrast z Kconfig, dłuższe opóźnienia)
 *  - Wysyłka DDRAM w paczkach (rozmiar z Kconfig)
 */

#include "lcd1602rgb_dfr_async.h"
#include "services_i2c.h"
#include "core_ev.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "DFR_LCD";

/* --- Fallbacki Kconfig (gdy nie ma sdkconfig, np. test host) --- */
#ifndef CONFIG_APP_LCD_CONTR_LOW
#define CONFIG_APP_LCD_CONTR_LOW 12
#endif
#ifndef CONFIG_APP_LCD_CONTR_HIGH
#define CONFIG_APP_LCD_CONTR_HIGH 1
#endif
#ifndef CONFIG_APP_LCD_INIT_FIRST_DELAY_MS
#define CONFIG_APP_LCD_INIT_FIRST_DELAY_MS 40
#endif
#ifndef CONFIG_APP_LCD_BURST_SIZE
#define CONFIG_APP_LCD_BURST_SIZE 8
#endif
#ifndef CONFIG_APP_LCD_INTERCHUNK_DELAY_MS
#define CONFIG_APP_LCD_INTERCHUNK_DELAY_MS 1
#endif

/* Kontrolny bajt ST7032: 0x00=command, 0x40=data */
#define LCD_CTL_CMD   0x00
#define LCD_CTL_DATA  0x40

/* Rozmiar ekranu */
#define LCD_COLS 16
#define LCD_ROWS 2

/* BURST z Kconfig (co najmniej 1) */
#undef  LCD_BURST
#define LCD_BURST ((CONFIG_APP_LCD_BURST_SIZE) < 1 ? 1 : (CONFIG_APP_LCD_BURST_SIZE))

/* Ramka 2x16 znaków ASCII. */
static char s_fb[LCD_ROWS][LCD_COLS];
static bool s_dirty = false;

/* Urządzenia I2C (z warstwy aplikacji) */
static i2c_dev_t* s_dev_lcd = NULL;
static i2c_dev_t* s_dev_rgb = NULL;

/* Kolejka eventów sterownika + one-shot do opóźnień */
static ev_queue_t s_q;
static esp_timer_handle_t s_delay = NULL;

/* Automat stanów */
typedef enum {
    ST_IDLE = 0,
    ST_RGB_INIT,
    ST_LCD_INIT,
    ST_LCD_READY,
    ST_LCD_FLUSH_POS,
    ST_LCD_FLUSH_DATA
} state_t;

static state_t s_st = ST_IDLE;
static uint8_t s_row = 0;          /* aktualny wiersz do flush */
static uint8_t s_col = 0;          /* kolumna startu (0) */
static uint8_t s_sent_in_row = 0;  /* ile znaków wysłaliśmy w bieżącym wierszu */

/* forward */
static void step(void);

/* prosty helper do submitu I2C (TX) – usługa kopiuje bufor */
static bool i2c_tx_now(i2c_dev_t* d, const uint8_t* data, size_t len) {
    i2c_req_t r = {
        .op = I2C_OP_TX, .dev = d, .tx = data, .txlen = len,
        .rx = NULL, .rxlen = 0, .timeout_ms = 50, .user = NULL
    };
    return services_i2c_submit(&r);
}

/* One-shot delay -> po jego wygaśnięciu robimy kolejny krok automatu */
static void after_delay_cb(void* arg) {
    (void)arg;
    ev_post(EV_SRC_LCD, EV_LCD_UPDATED /* używane jako „DELAY_DONE” */, 0, 0);
}
static void delay_ms(uint32_t ms) {
    if (!s_delay) {
        const esp_timer_create_args_t a = { .callback = after_delay_cb, .name = "lcd_delay" };
        esp_timer_create(&a, &s_delay);
    }
    esp_timer_stop(s_delay);
    esp_timer_start_once(s_delay, (uint64_t)ms * 1000ULL);
}

/* --- Niskopoziomowe pomocniki LCD --- */
static bool lcd_cmd(uint8_t cmd) {
    uint8_t buf[2] = { LCD_CTL_CMD, cmd };
    return i2c_tx_now(s_dev_lcd, buf, 2);
}

/* Wyślij paczkę danych (max LCD_BURST) – jedna transakcja I2C */
static bool lcd_data_chunk(const uint8_t* d, size_t n) {
    if (n > LCD_BURST) n = LCD_BURST;
    uint8_t tmp[1 + LCD_BURST];
    tmp[0] = LCD_CTL_DATA;
    memcpy(&tmp[1], d, n);
    return i2c_tx_now(s_dev_lcd, tmp, 1 + n);
}

/* ustaw adres DDRAM (bit 7=1) */
static bool lcd_set_ddram(uint8_t addr) {
    return lcd_cmd(0x80 | (addr & 0x7F));
}

/* --- Kontroler RGB (PCA9633-kompatybilny) --- */
static bool rgb_init_sequence(void) {
    /* MODE1=0x00 (wake), MODE2=0x05, LEDOUT=0xAA (kanały PWM) */
    const uint8_t seq1[] = { 0x00, 0x00 };
    const uint8_t seq2[] = { 0x01, 0x05 };
    const uint8_t seq3[] = { 0x08, 0xAA };
    return i2c_tx_now(s_dev_rgb, seq1, sizeof(seq1))
        && i2c_tx_now(s_dev_rgb, seq2, sizeof(seq2))
        && i2c_tx_now(s_dev_rgb, seq3, sizeof(seq3));
}
static void rgb_set(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t wr[2];
    wr[0] = 0x04; wr[1] = r; i2c_tx_now(s_dev_rgb, wr, 2); /* RED   (PWM3) */
    wr[0] = 0x03; wr[1] = g; i2c_tx_now(s_dev_rgb, wr, 2); /* GREEN (PWM2) */
    wr[0] = 0x02; wr[1] = b; i2c_tx_now(s_dev_rgb, wr, 2); /* BLUE  (PWM1) */
}

/* --- Sekwencja inicjalizacji ST7032 dla 3.3V --- */
static bool lcd_init_next_step(uint8_t *out_delay_ms, bool *out_finished) {
    static int idx = 0;
    *out_delay_ms = 0;
    *out_finished = false;

    /* Notacja (IS=instr set):
       - 0x38 : Function set (IS=0, 2-line)
       - 0x39 : Function set (IS=1)
       - 0x14 : Internal OSC freq
       - 0x70|C[3:0] : Contrast low
       - 0x5C..0x5F : ICON/BOOST + Contrast high (C[5:4])
       - 0x6C : Follower ON (wymaga ~200ms dla stabilizacji V0)
       - 0x38 : IS=0
       - 0x0C : Display ON
       - 0x01 : Clear (≥1.6ms)
       - 0x06 : Entry mode (I/D=1, S=0)
    */
    const uint8_t c_lo = (uint8_t)(CONFIG_APP_LCD_CONTR_LOW & 0x0F);
    const uint8_t c_hi = (uint8_t)(CONFIG_APP_LCD_CONTR_HIGH & 0x03);
    const uint8_t contr_high_cmd = (uint8_t)(0x58 | (c_hi << 2) | 0x03); // ICON=1, BOOST=1

    switch (idx) {
        case 0:  if (!lcd_cmd(0x38)) return false; *out_delay_ms = CONFIG_APP_LCD_INIT_FIRST_DELAY_MS; idx++; return true;
        case 1:  if (!lcd_cmd(0x39)) return false; *out_delay_ms = 2;   idx++; return true;
        case 2:  if (!lcd_cmd(0x14)) return false; *out_delay_ms = 2;   idx++; return true;
        case 3:  if (!lcd_cmd(0x70 | c_lo)) return false; *out_delay_ms = 2; idx++; return true;
        case 4:  if (!lcd_cmd(contr_high_cmd)) return false; *out_delay_ms = 2; idx++; return true;
        case 5:  if (!lcd_cmd(0x6C)) return false; *out_delay_ms = 200; idx++; return true;
        case 6:  if (!lcd_cmd(0x38)) return false; *out_delay_ms = 2;   idx++; return true;
        case 7:  if (!lcd_cmd(0x0C)) return false; *out_delay_ms = 2;   idx++; return true;
        case 8:  if (!lcd_cmd(0x01)) return false; *out_delay_ms = 3;   idx++; return true;
        case 9:  if (!lcd_cmd(0x06)) return false; *out_delay_ms = 2;   idx++; return true;
        default: idx = 0; *out_finished = true; return true;
    }
}

/* --- Główny automat sterownika --- */
static void step(void) {
    switch (s_st) {
        case ST_IDLE:
            break;

        case ST_RGB_INIT:
            if (rgb_init_sequence()) {
                s_st = ST_LCD_INIT;
                delay_ms(5);
            } else {
                ESP_LOGW(TAG, "RGB init submit failed");
            }
            break;

        case ST_LCD_INIT: {
            uint8_t dly = 0; bool finished = false;
            bool submitted = lcd_init_next_step(&dly, &finished);
            if (!submitted) {
                ESP_LOGW(TAG, "LCD init submit failed");
                break;
            }
            if (finished) {
                s_st = ST_LCD_READY;
                ev_post(EV_SRC_LCD, EV_LCD_READY, 0, 0);
            } else {
                delay_ms(dly ? dly : 1);
            }
        } break;

        case ST_LCD_READY:
            /* czekamy na flush() lub robimy test (obsłużone w task_ev po EV_LCD_READY) */
            break;

        case ST_LCD_FLUSH_POS: {
            const uint8_t addr = (s_row == 0 ? 0x00 : 0x40) + s_col;
            s_sent_in_row = 0;
            if (lcd_set_ddram(addr)) {
                s_st = ST_LCD_FLUSH_DATA;
            }
        } break;

        case ST_LCD_FLUSH_DATA: {
            /* Wyślij wiersz w paczkach (z pauzą konfigurowalną) */
            if (s_row < LCD_ROWS) {
                size_t left = LCD_COLS - s_sent_in_row;
                size_t chunk = (left > LCD_BURST) ? LCD_BURST : left;
                if (lcd_data_chunk((const uint8_t*)&s_fb[s_row][s_sent_in_row], chunk)) {
                    s_sent_in_row += chunk;
                    if (s_sent_in_row >= LCD_COLS) {
                        s_row++;
                        if (s_row >= LCD_ROWS) {
                            s_st   = ST_IDLE;
                            s_dirty = false;
                            ev_post(EV_SRC_LCD, EV_LCD_UPDATED, 0, 0);
                        } else {
                            s_st = ST_LCD_FLUSH_POS;
                        }
                    } else {
                        if (CONFIG_APP_LCD_INTERCHUNK_DELAY_MS > 0) {
                            delay_ms(CONFIG_APP_LCD_INTERCHUNK_DELAY_MS);
                            /* wrócimy na EV_LCD_UPDATED */
                        }
                    }
                }
            } else {
                s_st = ST_IDLE;
            }
        } break;

        default: break;
    }
}

/* Task – odbiera eventy i pcha automat (czysty event loop) */
static void task_ev(void* arg) {
    (void)arg;
    ev_msg_t m;
    for (;;) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) == pdTRUE) {
            if (m.src == EV_SRC_SYS && m.code == EV_SYS_START) {
                ESP_LOGI(TAG, "LCD/RGB: start init");
                s_st = ST_RGB_INIT;
                step();
            } else if (m.src == EV_SRC_I2C && m.code == EV_I2C_DONE) {
                /* Tu świadomie NIC nie robimy – automat sterowany jest delayami one-shot. */
            } else if (m.src == EV_SRC_LCD && m.code == EV_LCD_READY) {
                /* DIAGNOSTYKA: ustaw biały i wypchnij prosty test raz */
                rgb_set(255, 255, 255);
                for (int r=0; r<LCD_ROWS; ++r) memset(s_fb[r], ' ', LCD_COLS);
                const char *l0 = "HELLO ST7032   ";
                const char *l1 = "DFR0464 v2.0   ";
                memcpy(s_fb[0], l0, strlen(l0) > LCD_COLS ? LCD_COLS : strlen(l0));
                memcpy(s_fb[1], l1, strlen(l1) > LCD_COLS ? LCD_COLS : strlen(l1));
                s_dirty = true;
                s_row = 0; s_col = 0; s_st = ST_LCD_FLUSH_POS;
                step();
            } else if (m.src == EV_SRC_LCD && m.code == EV_LCD_UPDATED) {
                step(); /* kontynuacja po opóźnieniach one-shot */
            }
        }
    }
}

/* --- API sterownika --- */

bool lcd1602rgb_init(const lcd1602rgb_cfg_t* cfg) {
    if (!cfg || !cfg->dev_lcd || !cfg->dev_rgb) return false;
    s_dev_lcd = cfg->dev_lcd;
    s_dev_rgb = cfg->dev_rgb;

    for (int r = 0; r < LCD_ROWS; ++r) memset(s_fb[r], ' ', LCD_COLS);
    s_dirty = false;

    if (!ev_subscribe(&s_q, 16)) return false;

    TaskHandle_t th = NULL;
    if (xTaskCreate(task_ev, "lcd_ev", 4096, NULL, 4, &th) != pdPASS) return false;
    return true;
}

void lcd1602rgb_set_rgb(uint8_t r, uint8_t g, uint8_t b) { rgb_set(r, g, b); }

void lcd1602rgb_draw_text(uint8_t col, uint8_t row, const char* utf8) {
    if (!utf8) return;
    if (row >= LCD_ROWS || col >= LCD_COLS) return;
    const size_t max = LCD_COLS - col;
    for (size_t i = 0; i < max; ++i) {
        char ch = utf8[i];
        if (ch == '\0' || ch == '\n') break;
        s_fb[row][col + i] = (ch >= 0x20 && ch <= 0x7E) ? ch : '?';
    }
    s_dirty = true;
}

void lcd1602rgb_request_flush(void) {
    if (!s_dirty) return;
    if (s_st != ST_IDLE && s_st != ST_LCD_READY) return;
    s_row = 0; s_col = 0; s_sent_in_row = 0;
    s_st  = ST_LCD_FLUSH_POS;
    step();
}
