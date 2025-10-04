/**
 * @file main.c
 * @brief Aplikacja demo LCD: asynchroniczny I²C + driver DFR0464 (0x3E/0x2D), full event-driven.
 *
 * @details
 *  - Clean Architecture:
 *      - warstwa "ports" (abstrakcje I/O, timer, log),
 *      - "services__*" (serwisy systemowe: timer, I2C kolejkujący),
 *      - "drivers__lcd1602rgb_dfr_async" (logika urządzenia, zdarzeniowa),
 *      - aplikacja – tylko orkiestracja i obsługa eventów.
 *
 * @dot
 * digraph Arch {
 *   rankdir=LR; node [shape=box, fontsize=10];
 *   subgraph cluster_app { label="App"; "app_main()" -> "EV loop"; }
 *   "EV loop" -> "services__timer";
 *   "EV loop" -> "services__i2c";
 *   "EV loop" -> "driver LCD";
 *   "services__i2c" -> "I2C Port (IDF)";
 *   "driver LCD" -> "services__i2c" [label="submit"];
 * }
 * @enddot
 *
 * @note Pinologia domyślna (zmienisz w menuconfig):
 *   - ESP32-C6: SDA=10, SCL=11
 *   - ESP32-C3: SDA=8,  SCL=9
 *   - ESP32/S3: SDA=21, SCL=22
 *   - ESP32-S2: SDA=33, SCL=35
 */

#include "sdkconfig.h"      // <<< potrzebne dla CONFIG_* (np. INFRA_LOG_CLI/START_REPL)
#include "core_ev.h"
#include "ports/log_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "idf_i2c_port.h" /* i2c_bus_probe_addr()  */
#include "lcd1602rgb_dfr_async.h"
#include "services_i2c.h"
#include "services_timer.h"

#if CONFIG_INFRA_LOG_CLI
#include "logging_cli.h"     /* CLI 'logrb' + (opcjonalny) REPL */
#endif

static const char* TAG = "APP";

/* Prosty skan – wybieramy rozpoznane adresy LCD (0x3E/0x3F) i RGB (0x2D/0x62).
 * Jeśli nie znajdziemy, użyjemy fallbacków z Kconfig. */
static void scan_log_and_pick_addrs(i2c_bus_t* bus, uint8_t* out_lcd, uint8_t* out_rgb)
{
    bool got_lcd = false, got_rgb = false;
    LOGI("DFR_LCD", "I2C scan begin");
    for (uint8_t a = 0x08; a <= 0x77; ++a)
    {
        bool ack = false;
        if (i2c_bus_probe_addr(bus, a, 50, &ack) == ESP_OK && ack)
        {
            LOGI("DFR_LCD", "found 0x%02X", a);
            if (!got_lcd && (a == 0x3E || a == 0x3F || a == CONFIG_APP_LCD_ADDR))
            {
                *out_lcd = a;
                got_lcd  = true;
            }
            if (!got_rgb && (a == 0x2D || a == 0x62 || a == CONFIG_APP_RGB_ADDR))
            {
                *out_rgb = a;
                got_rgb  = true;
            }
        }
    }
    LOGI("DFR_LCD", "I2C scan end");

    if (!got_lcd)
        *out_lcd = (uint8_t)CONFIG_APP_LCD_ADDR;
    if (!got_rgb)
        *out_rgb = (uint8_t)CONFIG_APP_RGB_ADDR;
}

void app_main(void)
{
    /* 1) Uruchom bus zdarzeń i serwisy systemowe (timer + kolejkujący I2C) */
    ev_init();
    services_timer_start();
    services_i2c_start(16, 4096, 8);

    /* 2) Utwórz magistralę I2C */
    i2c_bus_t*    bus    = NULL;
    i2c_bus_cfg_t buscfg = {
        .sda_gpio               = CONFIG_APP_I2C_SDA,
        .scl_gpio               = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz                 = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK(i2c_bus_create(&buscfg, &bus));

    /* 3) Auto‑detect adresów (z fallbackiem do Kconfig) */
    uint8_t lcd_addr = 0, rgb_addr = 0;
    scan_log_and_pick_addrs(bus, &lcd_addr, &rgb_addr);

    /* 4) Dodaj urządzenia pod wykrytymi adresami */
    i2c_dev_t *dev_lcd = NULL, *dev_rgb = NULL;
    ESP_ERROR_CHECK(i2c_dev_add(bus, lcd_addr, &dev_lcd));
    ESP_ERROR_CHECK(i2c_dev_add(bus, rgb_addr, &dev_rgb));

    /* 5) Start drivera LCD */
    lcd1602rgb_cfg_t lc = { .dev_lcd = dev_lcd, .dev_rgb = dev_rgb };
    if (!lcd1602rgb_init(&lc)) {
        LOGE(TAG, "LCD init register failed");
    }

    /* 6) Subskrypcja eventów aplikacji */
    ev_queue_t q;
    ev_subscribe(&q, 16);

    /* 7) Rozruch systemu – pierwszy "tick" dla drivera */
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    /* 8) (Opcjonalnie) udostępnij CLI logów przez esp_console:
          - jeśli REPL włączony, startujemy go i rejestrujemy komendę,
          - jeśli REPL wyłączony, zarejestruj tylko komendę.
     */
#if CONFIG_INFRA_LOG_CLI
  #if CONFIG_INFRA_LOG_CLI_START_REPL
    infra_log_cli_start_repl();   // start REPL + rejestracja komendy
  #else
    infra_log_cli_register();     // tylko rejestracja komendy (bez REPL)
  #endif
#endif

    /* 9) Główna pętla zdarzeń */
    ev_msg_t m;
    bool     once = false;
    for (;;)
    {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE)
        {
            if (m.src == EV_SRC_LCD && m.code == EV_LCD_READY && !once)
            {
                /* Pierwszy flush po zainicjalizowaniu */
                lcd1602rgb_draw_text(0, 0, CONFIG_APP_LCD_TEXT0);
                lcd1602rgb_draw_text(0, 1, CONFIG_APP_LCD_TEXT1);
                lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
                lcd1602rgb_request_flush();
                once = true;
                LOGI(TAG, "LCD gotowy – wysłano pierwszy ekran.");
            }
            else if (m.src == EV_SRC_TIMER && m.code == EV_TICK_1S)
            {
                LOGI(TAG, "[%u ms] tick", (unsigned)m.t_ms);
            }
        }
    }
}
