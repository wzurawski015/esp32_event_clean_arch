/**
 * @file main.c
 * @brief Entry point aplikacji demo_lcd_rgb.
 *
 * Integracja wszystkich warstw "Industrial Grade":
 * - Ports: UART, I2C, WDT, KV (NVS), Log, LED, Internal Temp.
 * - Services: Timer, I2C Worker, UART Batching, LED Animation, Sys Temp Monitor.
 * - App: Log Bus (Zero-Copy), LCD Actor, CLI.
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h" // Dla esp_reset_reason()
#include "nvs_flash.h"
#include "nvs.h"

/* Core & Ports */
#include "core_ev.h"
#include "core/leasepool.h"
#include "ports/kv_port.h"
#include "ports/log_port.h"
#include "ports/wdt_port.h"

/* Services */
#include "services_timer.h"
#include "services_i2c.h"
#include "services_uart.h"
#include "services_led.h"
#include "services_internal_temp.h" // <--- NOWOŚĆ: Czujnik wbudowany

/* Application Actors */
#include "app_log_bus.h"
#include "app_demo_lcd.h"

#if CONFIG_INFRA_LOG_CLI
#include "logging_cli.h"
#endif

static const char* TAG = "MAIN";

static void set_default_log_levels(void)
{
    esp_log_level_set("*",       ESP_LOG_INFO);
    esp_log_level_set("APP",     ESP_LOG_WARN);
    esp_log_level_set("LOGCLI",  ESP_LOG_INFO);
    esp_log_level_set("DFR_LCD", ESP_LOG_DEBUG);
    esp_log_level_set("SVC_UART", ESP_LOG_INFO);
    esp_log_level_set("SVC_LED",  ESP_LOG_INFO);
    esp_log_level_set("SVC_ITEMP",ESP_LOG_INFO); // Logi serwisu temperatury
    esp_log_level_set("NVS_ADP", ESP_LOG_INFO);
}

/* --- POST-MORTEM DIAGNOSTICS --- */
static void check_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    kv_handle_t* kv = NULL;
    kv_cfg_t cfg = { .namespace_name = "storage" };
    
    if (kv_open(&cfg, &kv) != PORT_OK) return;

    int32_t crash_cnt = 0;
    kv_get_int(kv, "crash_cnt", &crash_cnt);

    if (reason == ESP_RST_TASK_WDT) {
        LOGE(TAG, "!!! SYSTEM RECOVERED FROM WATCHDOG TIMEOUT !!!");
        crash_cnt++;
        kv_set_int(kv, "crash_cnt", crash_cnt);
        kv_commit(kv);
    } 
    else if (reason == ESP_RST_PANIC) {
        LOGE(TAG, "!!! SYSTEM RECOVERED FROM EXCEPTION/PANIC !!!");
        crash_cnt++;
        kv_set_int(kv, "crash_cnt", crash_cnt);
        kv_commit(kv);
    }
    else {
        LOGI(TAG, "Boot reason: %d (Normal)", reason);
    }

    if (crash_cnt > 0) {
        LOGW(TAG, "Total unexpected crashes so far: %ld", (long)crash_cnt);
    }
    
    kv_close(kv);
}

/* --- DIAMOND EDITION VERIFICATION TEST --- */
static void run_diamond_nvs_test(void)
{
    LOGI(TAG, "--- START NVS DIAMOND TEST ---");
    kv_handle_t* kv = NULL;
    kv_cfg_t cfg = { .partition_name = "nvs", .namespace_name = "storage", .read_only = false };
    if (kv_open(&cfg, &kv) != PORT_OK) { LOGE(TAG, "KV Open failed!"); return; }
    int32_t boot_cnt = 0;
    kv_get_int(kv, "boot_cnt", &boot_cnt);
    boot_cnt++;
    kv_set_int(kv, "boot_cnt", boot_cnt);
    kv_commit(kv);
    LOGI(TAG, "Boot count: %ld", (long)boot_cnt);
    kv_close(kv);
    LOGI(TAG, "--- END NVS DIAMOND TEST ---");
}

void app_main(void)
{
    set_default_log_levels();

    // 0. Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Diagnostyka
    check_reset_reason();
    run_diamond_nvs_test();

    // 2. Core Init
    ev_init();
    lp_init();

    // 3. Watchdog Init (5s)
    if (wdt_init(5000) != PORT_OK) {
        LOGE(TAG, "Critical: WDT Init Failed!");
    } else {
        LOGI(TAG, "Sentinel active: TWDT=5000ms");
    }

    const ev_bus_t* bus = ev_bus_default();

    // 4. Start Serwisów Infrastrukturalnych
    services_timer_start(bus);
    services_i2c_start(bus, 16, 4096, 8);
    
    uart_svc_cfg_t ucfg = {
        .uart_num = 1, .tx_pin = 4, .rx_pin = 5, .baud_rate = 115200, .pattern_char = '\n'
    };
    services_uart_start(bus, &ucfg);

    // Serwis LED (GPIO 8)
    led_svc_cfg_t led_cfg = { .gpio_num = 8, .max_leds = 1, .led_type = LED_SVC_WS2812 };
    if (services_led_start(bus, &led_cfg)) {
        LOGI(TAG, "LED Service active (GPIO 8)");
    } else {
        LOGE(TAG, "LED Service failed!");
    }

    // --- NOWOŚĆ: Serwis Temperatury Systemowej (CPU) ---
    // Będzie publikował EV_SYS_TEMP_UPDATE co 1000ms
    internal_temp_svc_cfg_t temp_cfg = { .period_ms = 1000 };
    if (services_internal_temp_start(bus, &temp_cfg)) {
        LOGI(TAG, "Internal Temp Service active (1000ms)");
    } else {
        LOGE(TAG, "Internal Temp Service failed!");
    }

    // 5. Start Aktorów Aplikacji (LCD wyświetla logi i temperaturę)
    app_log_bus_start(bus);
    app_demo_lcd_start(bus);

    // 6. CLI
#if CONFIG_INFRA_LOG_CLI
  #if CONFIG_INFRA_LOG_CLI_START_REPL
    infra_log_cli_start_repl();
  #else
    infra_log_cli_register();
  #endif
#endif

    vTaskDelete(NULL);
}

