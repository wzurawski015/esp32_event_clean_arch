#include "app_demo_lcd.h"

#include "core_ev.h"
#include "infra_log_stream.h"
#include "ports/log_port.h"

#include "idf_i2c_port.h"                 // i2c_bus_create/probe/add
#include "services_i2c.h"
#include "lcd1602rgb_dfr_async.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char* TAG = "APP_DEMO_LCD";

// Lokalne zasoby tego aktora
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

// Skraca/podkłada spacjami do 16 kolumn (LCD 16x2)
static void lcd_print_line16(uint8_t row, const char* s, size_t len)
{
    char tmp[17];
    size_t n = len > 16 ? 16 : len;
    memcpy(tmp, s, n);
    if (n < 16) memset(tmp + n, ' ', 16 - n);
    tmp[16] = '\0';
    lcd1602rgb_draw_text(0, row, tmp);
}

static inline void tail16_push_(char* buf, size_t* len_io, char c)
{
    if (*len_io < 16) {
        buf[*len_io] = c;
        (*len_io)++;
        return;
    }

    // okno przesuwne: utrzymuj ostatnie 16 znaków
    memmove(buf, buf + 1, 15);
    buf[15] = c;
}

static void drain_log_stream_to_lcd_(void)
{
    // Stan przenoszony pomiędzy wywołaniami: linia może być pocięta na fragmenty
    static char   tail[16];
    static size_t tail_len = 0;

    for (;;) {
        size_t n = 0;
        const uint8_t* p = infra_log_stream_peek(&n);
        if (!p || n == 0) break;

        for (size_t i = 0; i < n; ++i) {
            const char c = (char)p[i];
            if (c == '\n') {
                // Wyświetl ogon (ostatnie 16 znaków) tak jak w trybie EV_LOG_NEW
                char tmp[17];
                memcpy(tmp, tail, tail_len);
                tmp[tail_len] = '\0';
                lcd_print_line16(1, tmp, tail_len);
                lcd1602rgb_request_flush();
                tail_len = 0;
                continue;
            }

            if (c == '\r') continue;
            tail16_push_(tail, &tail_len, c);
        }

        infra_log_stream_consume(n);
    }
}

static void app_demo_lcd_task(void* arg)
{
    (void)arg;
    ev_queue_t q;
    ev_subscribe(&q, 16);

    // Kick start
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    bool first_ready = false;
    ev_msg_t m;

    for (;;) {
        if (xQueueReceive(q, &m, portMAX_DELAY) != pdTRUE) continue;

        // 1) Driver zgłosił gotowość LCD → ekran powitalny
        if (m.src == EV_SRC_LCD && m.code == EV_LCD_READY && !first_ready) {
            lcd_print_line16(0, CONFIG_APP_LCD_TEXT0, strlen(CONFIG_APP_LCD_TEXT0));
            lcd_print_line16(1, CONFIG_APP_LCD_TEXT1, strlen(CONFIG_APP_LCD_TEXT1));
            lcd1602rgb_set_rgb(CONFIG_APP_RGB_R, CONFIG_APP_RGB_G, CONFIG_APP_RGB_B);
            lcd1602rgb_request_flush();
            first_ready = true;
            LOGI(TAG, "LCD gotowy – wysłano ekran startowy.");
            continue;
        }

        // 2) Reaktywne logi (STREAM): EV_LOG_READY -> payload w ring-bufferze
        if (m.src == EV_SRC_LOG && m.code == EV_LOG_READY) {
            drain_log_stream_to_lcd_();
            continue;
        }

        // 2b) Legacy: EV_LOG_NEW (LEASE) – zachowane dla kompatybilności
        if (m.src == EV_SRC_LOG && m.code == EV_LOG_NEW) {
            lp_handle_t h = lp_unpack_handle_u32(m.a0);
            lp_view_t   v;
            if (lp_acquire(h, &v)) {
                const char* p = (const char*)v.ptr;
                size_t n = v.len;
                const char* start = (n > 16) ? (p + (n - 16)) : p;
                lcd_print_line16(1, start, (n > 16) ? 16 : n);
                lcd1602rgb_request_flush();
                lp_release(h);
            }
            continue;
        }

    }
}

bool app_demo_lcd_start(void)
{
    // Magistrala I²C
    i2c_bus_cfg_t buscfg = {
        .sda_gpio               = CONFIG_APP_I2C_SDA,
        .scl_gpio               = CONFIG_APP_I2C_SCL,
        .enable_internal_pullup = CONFIG_APP_I2C_PULLUP,
        .clk_hz                 = CONFIG_APP_I2C_HZ
    };
    ESP_ERROR_CHECK(i2c_bus_create(&buscfg, &s_bus));

    // Auto-detekcja adresów + dodanie urządzeń
    uint8_t lcd_addr = 0, rgb_addr = 0;
    scan_log_and_pick_addrs(s_bus, &lcd_addr, &rgb_addr);
    ESP_ERROR_CHECK(i2c_dev_add(s_bus, lcd_addr, &s_dev_lcd));
    ESP_ERROR_CHECK(i2c_dev_add(s_bus, rgb_addr, &s_dev_rgb));

    // Start drivera LCD
    lcd1602rgb_cfg_t lc = { .dev_lcd = s_dev_lcd, .dev_rgb = s_dev_rgb };
    if (!lcd1602rgb_init(&lc)) {
        LOGE(TAG, "LCD init failed");
        return false;
    }

    // Zadanie-aktor z pętlą zdarzeń
    if (xTaskCreate(app_demo_lcd_task, "app_demo_lcd", 4096, NULL, tskIDLE_PRIORITY+2, &s_task) != pdPASS) {
        LOGE(TAG, "create task failed");
        return false;
    }

    LOGI(TAG, "started");
    return true;
}
