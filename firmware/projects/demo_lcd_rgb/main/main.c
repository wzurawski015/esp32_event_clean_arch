/**
 * @file main.c
 * @brief Aplikacja demo LCD: nowy I2C + driver DFR0464 (0x3E/0x2D), full event-driven.
 *
 * Domyślne piny (zmienisz w menuconfig):
 *  - ESP32-C6: SDA=10, SCL=11
 *  - ESP32-C3: SDA=8,  SCL=9
 *  - ESP32/S3: SDA=21, SCL=22
 *  - ESP32-S2: SDA=33, SCL=35
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

static void scan_log_and_pick_addrs(i2c_bus_t* bus, uint8_t* lcd_addr, uint8_t* rgb_addr)
{
    ESP_LOGI("DFR_LCD", "I2C scan begin");
    for (uint8_t a = 0x03; a <= 0x77; ++a) {
        bool ack = false;
        if (i2c_bus_probe_addr(bus, a, 50, &ack) == ESP_OK && ack) {
            ESP_LOGI("DFR_LCD", "found 0x%02X", a);
        }
    }
    ESP_LOGI("DFR_LCD", "I2C scan end");

    /* preferencje adresów DFRobot */
    uint8_t try_lcd[] = { 0x3E, CONFIG_APP_LCD_ADDR };
    uint8_t try_rgb[] = { 0x2D, 0x62, CONFIG_APP_RGB_ADDR };

    bool ack = false;
    for (size_t i=0;i<sizeof(try_lcd);++i) {
        if (i2c_bus_probe_addr(bus, try_lcd[i], 50, &ack)==ESP_OK && ack) { *lcd_addr = try_lcd[i]; break; }
    }
    for (size_t i=0;i<sizeof(try_rgb);++i) {
        if (i2c_bus_probe_addr(bus, try_rgb[i], 50, &ack)==ESP_OK && ack) { *rgb_addr = try_rgb[i]; break; }
    }
}

void app_main(void)
{
    ev_init();
    services_timer_start();
    services_i2c_start(16, 4096, 8);

    /* Utwórz magistralę I2C */
    i2c_bus_t* bus=NULL;
    i2c_bus_cfg_t buscfg = {
        .sda_gpio = CONFIG_APP_I2C_SDA,
        .scl_gpio = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz   = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK( i2c_bus_create(&buscfg, &bus) );

    /* Skanuj i wybierz adresy */
    uint8_t lcd_addr = CONFIG_APP_LCD_ADDR;
    uint8_t rgb_addr = CONFIG_APP_RGB_ADDR;
    scan_log_and_pick_addrs(bus, &lcd_addr, &rgb_addr);

    /* Dodaj urządzenia */
    i2c_dev_t *dev_lcd=NULL, *dev_rgb=NULL;
    ESP_ERROR_CHECK( i2c_dev_add(bus, lcd_addr, &dev_lcd) );
    ESP_ERROR_CHECK( i2c_dev_add(bus, rgb_addr, &dev_rgb) );

    /* Start drivera LCD */
    lcd1602rgb_cfg_t lc = { .dev_lcd=dev_lcd, .dev_rgb=dev_rgb };
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
                /* Pierwszy flush po zainicjalizowaniu */
                lcd1602rgb_draw_text(0,0, CONFIG_APP_LCD_TEXT0);
                lcd1602rgb_draw_text(0,1, CONFIG_APP_LCD_TEXT1);
                lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
                lcd1602rgb_request_flush();
                once=true;
                ESP_LOGI(TAG, "LCD gotowy – wysłano pierwszy ekran.");
            }else if (m.src==EV_SRC_TIMER && m.code==EV_TICK_1S){
                ESP_LOGI(TAG, "[%u ms] tick", (unsigned)m.t_ms);
            }else if (m.src==EV_SRC_I2C && m.code==EV_I2C_ERROR){
                ESP_LOGW(TAG, "I2C error: %u", (unsigned)m.a1);
            }
        }
    }
}
