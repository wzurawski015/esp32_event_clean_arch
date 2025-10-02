/**
 * @file main.c
 * @brief Demo LCD: nowy I2C + DFR0464 (0x3E/0x2D), full event-driven, auto-scan I2C.
 *
 * GPIO (domyślne z Kconfig; dla ESP32-C6 faktycznie: SDA=10, SCL=11)
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_ev.h"
#include "services_timer.h"
#include "services_i2c.h"
#include "idf_i2c_port.h"
#include "lcd1602rgb_dfr_async.h"
#include "esp_log.h"

static const char* TAG="APP";

static void scan_log_and_pick_addrs(i2c_bus_t* bus, uint8_t* out_lcd, uint8_t* out_rgb)
{
    ESP_LOGI("DFR_LCD", "I2C scan begin");
    bool found_lcd = false, found_rgb = false;
    // Przejdź po pełnym zakresie 7-bit i zaloguj trafienia
    for (uint8_t a = 3; a <= 0x77; ++a) {
        bool ack = false;
        esp_err_t e = i2c_bus_probe_addr(bus, a, 50, &ack);
        if (e == ESP_OK && ack) {
            ESP_LOGI("DFR_LCD", "found 0x%02X", a);
            if (a == 0x3E) { *out_lcd = 0x3E; found_lcd = true; }
            if (a == 0x2D || a == 0x62) { *out_rgb = a; found_rgb = true; }
        }
    }
    if (!found_lcd) *out_lcd = (uint8_t)CONFIG_APP_LCD_ADDR;   // fallback z Kconfig
    if (!found_rgb) *out_rgb = (uint8_t)CONFIG_APP_RGB_ADDR;   // fallback z Kconfig
    ESP_LOGI("DFR_LCD", "I2C scan end");
}

void app_main(void)
{
    ev_init();
    services_timer_start();
    services_i2c_start(16, 4096, 8);

    /* Utwórz magistralę I2C */
    i2c_bus_t* bus = NULL;
    i2c_bus_cfg_t buscfg = {
        .sda_gpio = CONFIG_APP_I2C_SDA,
        .scl_gpio = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz   = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK( i2c_bus_create(&buscfg, &bus) );

    /* Auto-scan: wybierz adresy LCD/RGB */
    uint8_t lcd_addr = 0x3E, rgb_addr = 0x2D;
    scan_log_and_pick_addrs(bus, &lcd_addr, &rgb_addr);

    /* Dodaj urządzenia pod wybranymi adresami */
    i2c_dev_t *dev_lcd = NULL, *dev_rgb = NULL;
    ESP_ERROR_CHECK( i2c_dev_add(bus, lcd_addr, &dev_lcd) );
    ESP_ERROR_CHECK( i2c_dev_add(bus, rgb_addr, &dev_rgb) );

    /* Start drivera LCD */
    lcd1602rgb_cfg_t lc = { .dev_lcd = dev_lcd, .dev_rgb = dev_rgb };
    if (!lcd1602rgb_init(&lc)) {
        ESP_LOGE(TAG, "LCD init register failed");
    }

    /* Subskrypcja eventów do logowania i reakcji */
    ev_queue_t q; ev_subscribe(&q, 16);

    /* Po starcie wyślij tekst i kolor */
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    ev_msg_t m;
    bool once=false;
    for(;;){
        if (xQueueReceive(q, &m, portMAX_DELAY)==pdTRUE){
            if (m.src==EV_SRC_LCD && m.code==EV_LCD_READY && !once){
                lcd1602rgb_draw_text(0,0, CONFIG_APP_LCD_TEXT0);
                lcd1602rgb_draw_text(0,1, CONFIG_APP_LCD_TEXT1);
                lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
                lcd1602rgb_request_flush();
                once=true;
                ESP_LOGI(TAG, "LCD gotowy – wysłano pierwszy ekran.");
            } else if (m.src==EV_SRC_TIMER && m.code==EV_TICK_1S){
                ESP_LOGI(TAG, "[%u ms] tick", (unsigned)m.t_ms);
            }
        }
    }
}
