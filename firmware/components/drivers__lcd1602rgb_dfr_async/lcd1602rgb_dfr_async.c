/**
 * @file lcd1602rgb_dfr_async.c
 * @brief Sterownik DFRobot LCD1602 RGB (DFR0464 v2.0) – nieblokujący, event-driven, bez vTaskDelay.
 *
 *  - LCD: ST7032 / AIP31068 (I²C, 7-bit addr zwykle 0x3E)
 *  - RGB: PCA9633‑compatible (I²C, 7-bit addr zwykle 0x2D) – *opcjonalnie*
 *
 *  Cechy:
 *   - FSM inicjalizacji LCD krok‑po‑kroku (IS=0/1, OSC, kontrast LOW/HIGH, booster, follower).
 *   - Pierwsze opóźnienie po 0x38 sterowane Kconfig (CONFIG_APP_LCD_INIT_FIRST_DELAY_MS).
 *   - Follower settle = 220 ms (sprawdzony wariant niezawodny).
 *   - Wysyłka DDRAM w porcjach (CONFIG_APP_LCD_BURST_SIZE) + pauza (CONFIG_APP_LCD_INTERCHUNK_DELAY_MS).
 *   - Bajt kontrolny komend LCD wybierany Kconfigiem:
 *         CONFIG_APP_LCD_CMD_CTRL_0X80=y  →  CMD=0x80 (częsty „stabilniejszy” wariant),
 *         CONFIG_APP_LCD_CMD_CTRL_0X80=n  →  CMD=0x00 (wariant „książkowy”).
 *
 *  Architektura:
 *   - Automaty stanów: ST_RGB_INIT? → ST_LCD_INIT → ST_LCD_READY → (flush) ST_LCD_FLUSH_POS → ST_LCD_FLUSH_DATA.
 *   - Opóźnienia wyłącznie przez esp_timer one-shot (after_delay_cb → EV_LCD_UPDATED jako „tick”).
 *   - I²C przez asynchroniczny serwis (services_i2c_submit) – driver nie czeka blokująco.
 *
 *  Bezpieczeństwo/czas:
 *   - Brak alokacji w ISR, brak sleep w logice; I²C serializowane przez warstwę services__i2c.
 *   - RGB może nie istnieć (NULL); LCD działa niezależnie.
 */

#include "lcd1602rgb_dfr_async.h"

#include <stdio.h>
#include <string.h>

#include "core_ev.h"
#include "ports/log_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services_i2c.h"

static const char* TAG = "DFR_LCD";

/* -------------------------------------------------------------------------- */
/*  Kconfig fallback (kompilacja hostowa / brak sdkconfig)                    */
/* -------------------------------------------------------------------------- */
#ifndef CONFIG_APP_LCD_CONTR_LOW
#define CONFIG_APP_LCD_CONTR_LOW 8 /* C[3:0] – domyślnie 8 */
#endif
#ifndef CONFIG_APP_LCD_CONTR_HIGH
#define CONFIG_APP_LCD_CONTR_HIGH 2 /* C[5:4] – domyślnie 2 */
#endif
#ifndef CONFIG_APP_LCD_INIT_FIRST_DELAY_MS
#define CONFIG_APP_LCD_INIT_FIRST_DELAY_MS 40
#endif
#ifndef CONFIG_APP_LCD_BURST_SIZE
#define CONFIG_APP_LCD_BURST_SIZE 16
#endif
#ifndef CONFIG_APP_LCD_INTERCHUNK_DELAY_MS
#define CONFIG_APP_LCD_INTERCHUNK_DELAY_MS 1
#endif
#ifndef CONFIG_APP_LCD_CMD_CTRL_0X80
/* Domyślnie włącz 0x80, bo często jest stabilniejszy na modułach ST7032/AIP31068 */
#define CONFIG_APP_LCD_CMD_CTRL_0X80 1
#endif
#ifndef CONFIG_APP_RGB_R
#define CONFIG_APP_RGB_R 16
#endif
#ifndef CONFIG_APP_RGB_G
#define CONFIG_APP_RGB_G 32
#endif
#ifndef CONFIG_APP_RGB_B
#define CONFIG_APP_RGB_B 64
#endif

/* -------------------------------------------------------------------------- */
/*  Bajty kontrolne ST7032/AIP31068                                           */
/* -------------------------------------------------------------------------- */
#define LCD_CTL_DATA 0x40
#if CONFIG_APP_LCD_CMD_CTRL_0X80
#define LCD_CTL_CMD 0x80 /* komenda przez 0x80 (Co=1,RS=0) */
#else
#define LCD_CTL_CMD 0x00 /* komenda przez 0x00 (Co=0,RS=0) */
#endif

/* Rozmiar ekranu */
#define LCD_COLS 16
#define LCD_ROWS 2

/* Ile danych wysyłamy w jednej transakcji do DDRAM (bez bajtu kontrolnego) */
#undef LCD_BURST
#define LCD_BURST \
    ((CONFIG_APP_LCD_BURST_SIZE) < 1 ? 1 : ((CONFIG_APP_LCD_BURST_SIZE) > 64 ? 64 : (CONFIG_APP_LCD_BURST_SIZE)))

/* Ramka 2x16 znaków ASCII */
static char s_fb[LCD_ROWS][LCD_COLS];
static bool s_dirty = false;

/* Urządzenia I²C (przychodzą z warstwy aplikacji) */
static i2c_dev_t* s_dev_lcd = NULL;
static i2c_dev_t* s_dev_rgb = NULL;

/* Kolejka eventów sterownika + one-shot do opóźnień */
static ev_queue_t         s_q;
static esp_timer_handle_t s_delay = NULL;

/* -------------------------------------------------------------------------- */
/*  Stan maszyny                                                              */
/* -------------------------------------------------------------------------- */
typedef enum
{
    ST_IDLE = 0,
    ST_RGB_INIT, /* opcjonalny */
    ST_LCD_INIT,
    ST_LCD_READY,
    ST_LCD_FLUSH_POS,
    ST_LCD_FLUSH_DATA
} state_t;

static state_t s_st          = ST_IDLE;
static uint8_t s_row         = 0; /* który wiersz flushujemy */
static uint8_t s_col         = 0; /* startowa kolumna (0) */
static uint8_t s_sent_in_row = 0; /* ile znaków wysłaliśmy w bieżącym wierszu */

/* forward */
static void step(void);

/* -------------------------------------------------------------------------- */
/*  I²C helper (asynchroniczny submit przez services__i2c)                    */
/* -------------------------------------------------------------------------- */
static bool i2c_tx_now(i2c_dev_t* d, const uint8_t* data, size_t len)
{
    if (!d || !data || len == 0)
        return false;
    i2c_req_t r = {
        .op = I2C_OP_TX, .dev = d, .tx = data, .txlen = len, .rx = NULL, .rxlen = 0, .timeout_ms = 50, .user = NULL};
    return services_i2c_submit(&r);
}

/* -------------------------------------------------------------------------- */
/*  One‑shot delay → po wygaśnięciu robimy kolejny krok automatu              */
/*  (sygnałujemy EV_LCD_UPDATED jako „tick” drivera)                          */
/* -------------------------------------------------------------------------- */
static void after_delay_cb(void* arg)
{
    (void)arg;
    ev_post(EV_SRC_LCD, EV_LCD_UPDATED /* sygnał „kontynuuj” */, 0, 0);
}
static void delay_ms(uint32_t ms)
{
    if (!s_delay)
    {
        const esp_timer_create_args_t a = {.callback = after_delay_cb, .name = "lcd_delay"};
        esp_err_t                     e = esp_timer_create(&a, &s_delay);
        (void)e;
    }
    esp_timer_stop(s_delay);
    esp_timer_start_once(s_delay, (uint64_t)ms * 1000ULL);
}

/* -------------------------------------------------------------------------- */
/*  Niskopoziomowe helpery LCD                                                */
/* -------------------------------------------------------------------------- */
/** @brief Wyślij jedną komendę LCD (z bajtem kontrolnym wg Kconfig). */
static bool lcd_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {LCD_CTL_CMD, cmd};
    return i2c_tx_now(s_dev_lcd, buf, 2);
}

/** @brief Wyślij paczkę danych DDRAM (max LCD_BURST) – jedna transakcja I²C. */
static bool lcd_data_chunk(const uint8_t* d, size_t n)
{
    if (n == 0)
        return true;
    if (n > LCD_BURST)
        n = LCD_BURST;
    uint8_t tmp[1 + LCD_BURST];
    tmp[0] = LCD_CTL_DATA;
    memcpy(&tmp[1], d, n);
    return i2c_tx_now(s_dev_lcd, tmp, 1 + n);
}

/** @brief Ustaw adres DDRAM (bit7=1). */
static bool lcd_set_ddram(uint8_t addr)
{
    return lcd_cmd((uint8_t)(0x80 | (addr & 0x7F)));
}

/* -------------------------------------------------------------------------- */
/*  Kontroler RGB (PCA9633‑kompatybilny) – *opcjonalny*                       */
/* -------------------------------------------------------------------------- */
static inline bool has_rgb(void)
{
    return s_dev_rgb != NULL;
}

static bool rgb_init_sequence(void)
{
    if (!has_rgb())
        return true; /* brak RGB → traktujemy jako sukces */
    /* MODE1=0x00, MODE2=0x05, LEDOUT=0xAA (kanały PWM) */
    const uint8_t seq1[] = {0x00, 0x00};
    const uint8_t seq2[] = {0x01, 0x05};
    const uint8_t seq3[] = {0x08, 0xAA};
    return i2c_tx_now(s_dev_rgb, seq1, sizeof(seq1)) && i2c_tx_now(s_dev_rgb, seq2, sizeof(seq2)) &&
           i2c_tx_now(s_dev_rgb, seq3, sizeof(seq3));
}
static void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!has_rgb())
        return;
    uint8_t wr[2];
    wr[0] = 0x04;
    wr[1] = r;
    (void)i2c_tx_now(s_dev_rgb, wr, 2); /* RED   */
    wr[0] = 0x03;
    wr[1] = g;
    (void)i2c_tx_now(s_dev_rgb, wr, 2); /* GREEN */
    wr[0] = 0x02;
    wr[1] = b;
    (void)i2c_tx_now(s_dev_rgb, wr, 2); /* BLUE  */
}

/* -------------------------------------------------------------------------- */
/*  Sekwencja inicjalizacji ST7032/AIP31068 dla 3.3V                          */
/*  Kluczowe: 0x5C | C_hi (ICON=0, BOOSTER=1, C5:C4=C_hi)                      */
/* -------------------------------------------------------------------------- */
/**
 * @brief Jeden krok sekwencji init LCD. Po sukcesie ustawia wymagane opóźnienie
 *        i informuje, czy init został zakończony.
 * @return false – submit I²C nie wszedł do kolejki (błąd); true – OK.
 */
static bool lcd_init_next_step(uint8_t* out_delay_ms, bool* out_finished)
{
    static int idx = 0;
    *out_delay_ms  = 0;
    *out_finished  = false;

    const uint8_t c_lo = (uint8_t)(CONFIG_APP_LCD_CONTR_LOW & 0x0F);  /* C[3:0]  */
    const uint8_t c_hi = (uint8_t)(CONFIG_APP_LCD_CONTR_HIGH & 0x03); /* C[5:4]  */

    const uint8_t cmd_contr_lo     = (uint8_t)(0x70 | c_lo);
    const uint8_t cmd_pwr_contr_hi = (uint8_t)(0x5C | c_hi); /* ICON=0, BOOSTER=1 */

    switch (idx)
    {
        case 0:
            if (!lcd_cmd(0x38))
                return false;
            *out_delay_ms = CONFIG_APP_LCD_INIT_FIRST_DELAY_MS;
            idx++;
            return true; /* IS=0, 2-line */
        case 1:
            if (!lcd_cmd(0x39))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* IS=1 */
        case 2:
            if (!lcd_cmd(0x14))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* OSC */
        case 3:
            if (!lcd_cmd(cmd_contr_lo))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* Contrast low */
        case 4:
            if (!lcd_cmd(cmd_pwr_contr_hi))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* BOOSTER + hi */
        case 5:
            if (!lcd_cmd(0x6C))
                return false;
            *out_delay_ms = 220;
            idx++;
            return true; /* Follower ON (potrzebuje ~220 ms) */
        case 6:
            if (!lcd_cmd(0x38))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* IS=0 */
        case 7:
            if (!lcd_cmd(0x0C))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* Display ON (D=1, C=B=0) */
        case 8:
            if (!lcd_cmd(0x01))
                return false;
            *out_delay_ms = 3;
            idx++;
            return true; /* Clear (>=1.64 ms) */
        case 9:
            if (!lcd_cmd(0x06))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* Entry mode (I/D=1, S=0) */
        case 10:
            if (!lcd_cmd(0x02))
                return false;
            *out_delay_ms = 2;
            idx++;
            return true; /* Return Home */
        default:
            idx           = 0;
            *out_finished = true;
            return true;
    }
}

/* -------------------------------------------------------------------------- */
/*  Główny automat                                                            */
/* -------------------------------------------------------------------------- */
static void step(void)
{
    switch (s_st)
    {
        case ST_IDLE:
            break;

        case ST_RGB_INIT:
            /* RGB jest opcjonalne – jeśli brak urządzenia, przechodzimy dalej */
            if (rgb_init_sequence())
            {
                s_st = ST_LCD_INIT;
                delay_ms(5); /* niewielki odstęp po zapisie rejestrów RGB */
            }
            else
            {
                LOGW(TAG, "RGB init submit failed");
            }
            break;

        case ST_LCD_INIT:
        {
            uint8_t dly       = 0;
            bool    finished  = false;
            bool    submitted = lcd_init_next_step(&dly, &finished);
            if (!submitted)
            {
                LOGW(TAG, "LCD init submit failed");
                break;
            }
            if (finished)
            {
                s_st = ST_LCD_READY;
                ev_post(EV_SRC_LCD, EV_LCD_READY, 0, 0);
            }
            else
            {
                delay_ms(dly ? dly : 1);
            }
        }
        break;

        case ST_LCD_READY:
            /* czekamy na flush() z aplikacji */
            break;

        case ST_LCD_FLUSH_POS:
        {
            const uint8_t addr = (uint8_t)((s_row == 0 ? 0x00 : 0x40) + s_col);
            s_sent_in_row      = 0;
            if (lcd_set_ddram(addr))
            {
                s_st = ST_LCD_FLUSH_DATA;
            }
            else
            {
                LOGW(TAG, "set DDRAM addr submit failed");
            }
        }
        break;

        case ST_LCD_FLUSH_DATA:
        {
            if (s_row < LCD_ROWS)
            {
                size_t left  = (size_t)LCD_COLS - s_sent_in_row;
                size_t chunk = (left > LCD_BURST) ? LCD_BURST : left;
                if (lcd_data_chunk((const uint8_t*)&s_fb[s_row][s_sent_in_row], chunk))
                {
                    s_sent_in_row = (uint8_t)(s_sent_in_row + chunk);
                    if (s_sent_in_row >= LCD_COLS)
                    {
                        s_row++;
                        if (s_row >= LCD_ROWS)
                        {
                            s_st    = ST_IDLE;
                            s_dirty = false;
                            ev_post(EV_SRC_LCD, EV_LCD_UPDATED, 0, 0); /* sygnał: flush done */
                        }
                        else
                        {
                            s_st = ST_LCD_FLUSH_POS; /* ustaw adres kolejnego wiersza */
                        }
                    }
                    else
                    {
                        /* ♦ Opcjonalna pauza między paczkami DDRAM */
                        if (CONFIG_APP_LCD_INTERCHUNK_DELAY_MS > 0)
                        {
                            delay_ms(CONFIG_APP_LCD_INTERCHUNK_DELAY_MS);
                            /* Po EV_LCD_UPDATED wrócimy tu i wyślemy następną porcję */
                            break;
                        }
                    }
                }
                else
                {
                    LOGW(TAG, "data chunk submit failed");
                }
            }
            else
            {
                s_st = ST_IDLE;
            }
        }
        break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */
/*  Pętla zdarzeń drivera (subskrybent magistrali EV)                         */
/* -------------------------------------------------------------------------- */
static void task_ev(void* arg)
{
    (void)arg;
    ev_msg_t m;
    for (;;)
    {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE)
            continue;

        if (m.src == EV_SRC_SYS && m.code == EV_SYS_START)
        {
            LOGI(TAG, "LCD/RGB: start init");
            s_st = ST_RGB_INIT; /* przejdzie przez RGB (jeśli jest), potem LCD */
            step();
        }
        else if (m.src == EV_SRC_I2C && (m.code == EV_I2C_DONE || m.code == EV_I2C_ERROR))
        {
            /* Zakończono transakcję I²C → próbuj kolejnego kroku FSM */
            step();
        }
        else if (m.src == EV_SRC_LCD && m.code == EV_LCD_READY)
        {
            /* Ustaw startowe podświetlenie wg Kconfig (jeśli RGB istnieje) */
            rgb_set((uint8_t)CONFIG_APP_RGB_R, (uint8_t)CONFIG_APP_RGB_G, (uint8_t)CONFIG_APP_RGB_B);
        }
        else if (m.src == EV_SRC_LCD && m.code == EV_LCD_UPDATED)
        {
            /* Tick drivera: powrót z one‑shot delay lub zakończony flush */
            step();
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  API sterownika                                                             */
/* -------------------------------------------------------------------------- */

bool lcd1602rgb_init(const lcd1602rgb_cfg_t* cfg)
{
    if (!cfg || !cfg->dev_lcd)
        return false; /* RGB opcjonalne */
    s_dev_lcd = cfg->dev_lcd;
    s_dev_rgb = cfg->dev_rgb; /* może być NULL */

    for (int r = 0; r < LCD_ROWS; ++r)
    {
        memset(s_fb[r], ' ', LCD_COLS);
    }
    s_dirty = false;

    if (!ev_subscribe(&s_q, 16))
        return false;

    TaskHandle_t th = NULL;
    if (xTaskCreate(task_ev, "lcd_ev", 4096, NULL, 4, &th) != pdPASS)
    {
        return false;
    }
    return true;
}

void lcd1602rgb_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    rgb_set(r, g, b);
}

void lcd1602rgb_draw_text(uint8_t col, uint8_t row, const char* utf8)
{
    if (!utf8)
        return;
    if (row >= LCD_ROWS || col >= LCD_COLS)
        return;

    const size_t max = (size_t)LCD_COLS - col;
    for (size_t i = 0; i < max; ++i)
    {
        char ch = utf8[i];
        if (ch == '\0' || ch == '\n')
            break;
        s_fb[row][col + (uint8_t)i] = (ch >= 0x20 && ch <= 0x7E) ? ch : '?';
    }
    s_dirty = true;
}

void lcd1602rgb_request_flush(void)
{
    if (!s_dirty)
        return;
    if (s_st != ST_IDLE && s_st != ST_LCD_READY)
        return;

    s_row         = 0;
    s_col         = 0;
    s_sent_in_row = 0;
    s_st          = ST_LCD_FLUSH_POS;
    step();
}
