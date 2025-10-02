/**
 * @file main.c
 * @brief Aplikacja demo LCD: nowy I2C + driver DFR0464 (0x3E/LCD, RGB auto/Kconfig), full event-driven.
 *
 * Pinologia DOMYŚLNA (konfigurowalna w menuconfig → App configuration → I2C (LCD)):
 *  - ESP32-C6: SDA=GPIO10, SCL=GPIO11
 *  - ESP32-C3: SDA=GPIO8,  SCL=GPIO9
 *  - ESP32/S3: SDA=GPIO21, SCL=GPIO22
 *  - ESP32-S2: SDA=GPIO33, SCL=GPIO35
 *
 * Adresy I²C urządzeń:
 *  - LCD  : CONFIG_APP_LCD_ADDR (typowo 0x3E; auto-weryfikacja skanem)
 *  - RGB  : CONFIG_APP_RGB_ADDR (0x62 lub 0x2D; auto-fallback skanem)
 *
 * Teksty / kolor startowy: CONFIG_APP_LCD_TEXT0/1, CONFIG_APP_RGB_R/G/B
 */

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core_ev.h"
#include "services_timer.h"
#include "services_i2c.h"
#include "idf_i2c_port.h"    // port I²C (adapter na driver/i2c_master)
#include "lcd1602rgb_dfr_async.h"
#include "esp_log.h"

static const char* TAG = "APP";

/* Skanuje 0x03..0x77, loguje "found 0x..", wybiera adresy LCD i RGB */
static void scan_log_and_pick_addrs(i2c_bus_t* bus, uint8_t* out_lcd, uint8_t* out_rgb)
{
    // 1) Pełny skan z logami identycznymi jak w działającym przykładzie
    ESP_LOGI("DFR_LCD", "I2C scan begin");
    for (uint8_t a = 0x03; a <= 0x77; ++a) {
        bool ack = false;
        esp_err_t e = i2c_bus_probe_addr(bus, a, 50, &ack);
        if (e == ESP_OK && ack) {
            ESP_LOGI("DFR_LCD", "found 0x%02X", a);
        }
    }
    ESP_LOGI("DFR_LCD", "I2C scan end");

    // 2) LCD – preferencja: Kconfig, ale jeżeli brak ACK, spróbuj 0x3E
    uint8_t lcd_addr = (uint8_t)CONFIG_APP_LCD_ADDR;
    bool lcd_ack = false;
    (void)i2c_bus_probe_addr(bus, lcd_addr, 50, &lcd_ack);
    if (!lcd_ack) {
        bool a3e = false;
        (void)i2c_bus_probe_addr(bus, 0x3E, 50, &a3e);
        if (a3e) {
            ESP_LOGW("DFR_LCD", "LCD @0x%02X not found; using 0x3E", (unsigned)CONFIG_APP_LCD_ADDR);
            lcd_addr = 0x3E;
        } else {
            ESP_LOGW("DFR_LCD", "LCD not found on bus (no ACK).");
        }
    }

    // 3) RGB – preferencja: Kconfig, fallback: 0x2D → 0x62
    uint8_t rgb_addr = (uint8_t)CONFIG_APP_RGB_ADDR;
    bool rgb_ack = false;
    (void)i2c_bus_probe_addr(bus, rgb_addr, 50, &rgb_ack);
    if (!rgb_ack) {
        bool a2d = false, a62 = false;
        (void)i2c_bus_probe_addr(bus, 0x2D, 50, &a2d);
        (void)i2c_bus_probe_addr(bus, 0x62, 50, &a62);
        if (a2d || a62) {
            uint8_t pick = a2d ? 0x2D : 0x62;
            ESP_LOGW("DFR_LCD", "RGB @0x%02X not found; using 0x%02X",
                     (unsigned)CONFIG_APP_RGB_ADDR, (unsigned)pick);
            rgb_addr = pick;
        } else {
            ESP_LOGW("DFR_LCD", "RGB not found on bus (no ACK).");
        }
    }

    *out_lcd = lcd_addr;
    *out_rgb = rgb_addr;

    ESP_LOGI("DFR_LCD", "Using LCD=0x%02X, RGB=0x%02X", (unsigned)*out_lcd, (unsigned)*out_rgb);
}

void app_main(void)
{
    /* 1) Event-bus + serwisy */
    ev_init();
    services_timer_start();
    services_i2c_start(16, 4096, 8);  // worker I²C (kolejka, stack, priorytet)

    /* 2) Magistrala I²C z Kconfig (pinologia jest poprawna dla C6: SDA=10, SCL=11) */
    i2c_bus_t* bus = NULL;
    i2c_bus_cfg_t buscfg = {
        .sda_gpio = CONFIG_APP_I2C_SDA,
        .scl_gpio = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK(i2c_bus_create(&buscfg, &bus));

    /* 3) Scan + auto-pick adresów */
    uint8_t lcd_addr = (uint8_t)CONFIG_APP_LCD_ADDR;
    uint8_t rgb_addr = (uint8_t)CONFIG_APP_RGB_ADDR;
    scan_log_and_pick_addrs(bus, &lcd_addr, &rgb_addr);

    /* 4) Rejestracja urządzeń z wybranymi adresami */
    i2c_dev_t *dev_lcd = NULL, *dev_rgb = NULL;
    ESP_ERROR_CHECK(i2c_dev_add(bus, lcd_addr, &dev_lcd));
    ESP_ERROR_CHECK(i2c_dev_add(bus, rgb_addr, &dev_rgb));

    /* 5) Sterownik LCD (nieblokujący, event-driven przez services__i2c) */
    lcd1602rgb_cfg_t lcd_cfg = { .dev_lcd = dev_lcd, .dev_rgb = dev_rgb };
    if (!lcd1602rgb_init(&lcd_cfg)) {
        ESP_LOGE(TAG, "LCD init register failed");
    }

    /* 6) Subskrypcja zdarzeń */
    ev_queue_t q;
    if (!ev_subscribe(&q, 16)) {
        ESP_LOGE(TAG, "ev_subscribe failed");
        vTaskDelete(NULL);
        return;
    }

    /* 7) Globalny start (inne komponenty ruszą po EV_SYS_START) */
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    /* 8) Pętla reaktywna */
    ev_msg_t m;
    bool first_screen_sent = false;

    for (;;) {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE) {

            if (!first_screen_sent && m.src == EV_SRC_LCD && m.code == EV_LCD_READY) {
                lcd1602rgb_draw_text(0, 0, CONFIG_APP_LCD_TEXT0);
                lcd1602rgb_draw_text(0, 1, CONFIG_APP_LCD_TEXT1);
                lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
                lcd1602rgb_request_flush();
                first_screen_sent = true;
                ESP_LOGI(TAG, "LCD gotowy – wysłano pierwszy ekran.");
            }
            else if (m.src == EV_SRC_TIMER && m.code == EV_TICK_1S) {
                ESP_LOGI(TAG, "[%u ms] tick", (unsigned)m.t_ms);
            }
        }
    }
}
