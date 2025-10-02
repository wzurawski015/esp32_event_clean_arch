/**
 * @file main.c
 * @brief Aplikacja demo LCD: nowy I2C + driver DFR0464 (0x3E/LCD, RGB z Kconfig), full event-driven.
 *
 * Pinologia DOMYŚLNA (konfigurowalna w menuconfig → App configuration → I2C (LCD)):
 *  - ESP32-C6: SDA=GPIO10, SCL=GPIO11   ← poprawne dla C6
 *  - ESP32-C3: SDA=GPIO8,  SCL=GPIO9
 *  - ESP32/S3: SDA=GPIO21, SCL=GPIO22
 *  - ESP32-S2: SDA=GPIO33, SCL=GPIO35
 *
 * Adresy I²C urządzeń:
 *  - LCD  : CONFIG_APP_LCD_ADDR   (typowo 0x3E)
 *  - RGB  : CONFIG_APP_RGB_ADDR   (0x62 lub 0x2D – ustaw w Kconfig komponentu LCD)
 *
 * Teksty / kolor startowy:
 *  - CONFIG_APP_LCD_TEXT0 / CONFIG_APP_LCD_TEXT1
 *  - CONFIG_APP_RGB_R / _G / _B
 */

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core_ev.h"
#include "services_timer.h"
#include "services_i2c.h"
#include "idf_i2c_port.h"
#include "lcd1602rgb_dfr_async.h"
#include "esp_log.h"

static const char* TAG = "APP";

void app_main(void)
{
    /* 1) Event-bus + serwisy */
    ev_init();
    services_timer_start();
    /* kolejka żądań I2C = 16, stos workera = 4096, priorytet = 8 */
    services_i2c_start(16, 4096, 8);

    /* 2) Magistrala I2C (zgodnie z Kconfig App configuration → I2C (LCD)) */
    i2c_bus_t* bus = NULL;
    i2c_bus_cfg_t buscfg = {
        .sda_gpio = CONFIG_APP_I2C_SDA,
        .scl_gpio = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK(i2c_bus_create(&buscfg, &bus));

    /* 3) Urządzenia I2C (adresy również z Kconfig) */
    i2c_dev_t *dev_lcd = NULL, *dev_rgb = NULL;
    ESP_ERROR_CHECK(i2c_dev_add(bus, (uint8_t)CONFIG_APP_LCD_ADDR, &dev_lcd));
    ESP_ERROR_CHECK(i2c_dev_add(bus, (uint8_t)CONFIG_APP_RGB_ADDR, &dev_rgb));

    /* 4) Sterownik LCD (nieblokujący, w pełni event-driven) */
    lcd1602rgb_cfg_t lcd_cfg = { .dev_lcd = dev_lcd, .dev_rgb = dev_rgb };
    if (!lcd1602rgb_init(&lcd_cfg)) {
        ESP_LOGE(TAG, "LCD init register failed");
    }

    /* 5) Subskrypcja zdarzeń */
    ev_queue_t q;
    if (!ev_subscribe(&q, 16)) {
        ESP_LOGE(TAG, "ev_subscribe failed");
        vTaskDelete(NULL);
        return;
    }

    /* 6) Globalny start systemu (komponenty ruszają po EV_SYS_START) */
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    /* 7) Pętla reaktywna */
    ev_msg_t m;
    bool first_screen_sent = false;

    for (;;) {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE) {

            /* Po pełnym init sterownika LCD ustaw ekran i kolor z Kconfig */
            if (!first_screen_sent && m.src == EV_SRC_LCD && m.code == EV_LCD_READY) {
                lcd1602rgb_draw_text(0, 0, CONFIG_APP_LCD_TEXT0);
                lcd1602rgb_draw_text(0, 1, CONFIG_APP_LCD_TEXT1);
                lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
                lcd1602rgb_request_flush();
                first_screen_sent = true;
                ESP_LOGI(TAG, "LCD gotowy – wysłano pierwszy ekran.");
            }

            /* Logi serwisowe – tyk co 1 s z services__timer */
            else if (m.src == EV_SRC_TIMER && m.code == EV_TICK_1S) {
                ESP_LOGI(TAG, "[%u ms] tick", (unsigned)m.t_ms);
            }

            /* (opcjonalnie) Możesz dopisać inne reakcje na EV_I2C_DONE, EV_LCD_UPDATED itd. */
        }
    }
}
