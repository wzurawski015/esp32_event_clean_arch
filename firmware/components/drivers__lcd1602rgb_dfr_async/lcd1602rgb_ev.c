#include "lcd1602rgb_ev.h"
#include "lcd1602rgb_dfr_async.h"
#include "idf_i2c_port.h"
#include "services_i2c.h"
#include "ports/log_port.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char* TAG = "LCD1602_EV";

static i2c_bus_t*   s_bus     = NULL;
static i2c_dev_t*   s_dev_lcd = NULL;
static i2c_dev_t*   s_dev_rgb = NULL;
static TaskHandle_t s_task    = NULL;

static void scan_log_and_pick_addrs(i2c_bus_t* bus, uint8_t* out_lcd, uint8_t* out_rgb)
{
    bool got_lcd = false, got_rgb = false;
    LOGI("DFR_LCD", "I2C scan begin");
    for (uint8_t a = 0x08; a <= 0x77; ++a) {
        bool ack = false;
        if (i2c_bus_probe_addr(bus, a, 50, &ack) == ESP_OK && ack) {
            LOGI("DFR_LCD", "found 0x%02X", a);
            if (!got_lcd && (a == 0x3E || a == 0x3F || a == CONFIG_APP_LCD_ADDR)) {
                *out_lcd = a; got_lcd = true;
            }
            if (!got_rgb && (a == 0x2D || a == 0x62 || a == CONFIG_APP_RGB_ADDR)) {
                *out_rgb = a; got_rgb = true;
            }
        }
    }
    LOGI("DFR_LCD", "I2C scan end");
    if (!got_lcd) *out_lcd = (uint8_t)CONFIG_APP_LCD_ADDR;
    if (!got_rgb) *out_rgb = (uint8_t)CONFIG_APP_RGB_ADDR;
}

static void draw_line16(uint8_t row, const char* s, size_t len)
{
    char tmp[17];
    size_t n = len > 16 ? 16 : len;
    memcpy(tmp, s, n);
    if (n < 16) memset(tmp + n, ' ', 16 - n);
    tmp[16] = '\0';
    lcd1602rgb_draw_text(0, row, tmp);
}

static void lcd_ev_task(void* arg)
{
    (void)arg;
    ev_queue_t q;
    ev_subscribe(&q, 16);

    ev_msg_t m;
    for (;;) {
        if (xQueueReceive(q, &m, portMAX_DELAY) != pdTRUE) continue;

        if (m.src == EV_SRC_SYS && m.code == EV_SYS_START) {
            i2c_bus_cfg_t buscfg = {
                .sda_gpio               = CONFIG_APP_I2C_SDA,
                .scl_gpio               = CONFIG_APP_I2C_SCL,
                .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
                .clk_hz                 = CONFIG_APP_I2C_HZ
            };
            ESP_ERROR_CHECK(i2c_bus_create(&buscfg, &s_bus));
            uint8_t lcd_addr = 0, rgb_addr = 0;
            scan_log_and_pick_addrs(s_bus, &lcd_addr, &rgb_addr);
            ESP_ERROR_CHECK(i2c_dev_add(s_bus, lcd_addr, &s_dev_lcd));
            ESP_ERROR_CHECK(i2c_dev_add(s_bus, rgb_addr, &s_dev_rgb));
            lcd1602rgb_cfg_t lc = { .dev_lcd = s_dev_lcd, .dev_rgb = s_dev_rgb };
            if (!lcd1602rgb_init(&lc)) {
                LOGE(TAG, "LCD init failed");
            } else {
                ev_post(EV_SRC_LCD, EV_LCD_READY, 0, 0);
            }
            continue;
        }

        if (m.src == EV_SRC_LCD && m.code == EV_LCD_CMD_SET_RGB) {
            uint8_t r,g,b;
            lcd_unpack_rgb(m.a0, &r, &g, &b);
            lcd1602rgb_set_rgb(r, g, b);
            continue;
        }

        if (m.src == EV_SRC_LCD && m.code == EV_LCD_CMD_DRAW_ROW) {
            lp_handle_t h = lp_unpack_handle_u32(m.a0);
            lp_view_t v;
            if (lp_acquire(h, &v)) {
                if (v.len >= sizeof(lcd_cmd_draw_row_hdr_t)) {
                    lcd_cmd_draw_row_hdr_t* hdr = (lcd_cmd_draw_row_hdr_t*)v.ptr;
                    const char* text = (const char*)(hdr + 1);
                    size_t text_len = v.len - sizeof(lcd_cmd_draw_row_hdr_t);
                    draw_line16(hdr->row, text, text_len);
                }
                lp_release(h);
            }
            continue;
        }

        if (m.src == EV_SRC_LCD && m.code == EV_LCD_CMD_FLUSH) {
            lcd1602rgb_request_flush();
            continue;
        }
    }
}

bool lcd1602rgb_ev_start(void)
{
    if (s_task) return true;
    if (xTaskCreate(lcd_ev_task, "lcd1602_ev", 4096, NULL, tskIDLE_PRIORITY+3, &s_task) != pdPASS) {
        LOGE(TAG, "create task failed");
        return false;
    }
    LOGI(TAG, "EV adapter started");
    return true;
}
