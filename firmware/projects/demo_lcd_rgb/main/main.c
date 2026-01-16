#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

/* --- Warstwa Core (Fundament) --- */
#include "core_ev.h"
#include "core/leasepool.h"

/* --- Warstwa Ports (Abstrakcje) --- */
#include "ports/kv_port.h"
#include "ports/log_port.h"

/* --- Warstwa Services (Logika Aplikacyjna / Use Cases) --- */
#include "services_timer.h"
#include "services_i2c.h"
#include "services_uart.h"

/* --- Warstwa Aktorów (Prezentacja / Interakcja) --- */
#include "app_log_bus.h"
#include "app_demo_lcd.h"

#if CONFIG_INFRA_LOG_CLI
#include "logging_cli.h"
#endif

static const char* TAG = "MAIN";

/* Domyślne poziomy logowania dla różnych modułów */
static void set_default_log_levels(void)
{
    esp_log_level_set("*",       ESP_LOG_INFO);
    esp_log_level_set("APP",     ESP_LOG_WARN);
    esp_log_level_set("LOGCLI",  ESP_LOG_INFO);
    esp_log_level_set("DFR_LCD", ESP_LOG_DEBUG);
    esp_log_level_set("SVC_UART", ESP_LOG_INFO);
    esp_log_level_set("NVS_ADP", ESP_LOG_INFO);
}

/* * Self-Test NVS (Diamond Edition).
 * Demonstruje użycie portu KV bez wiedzy o tym, że pod spodem jest ESP32 NVS.
 */
typedef struct {
    uint32_t boot_magic;
    uint8_t  last_user_id;
    char     device_name[16];
} app_config_blob_t;

static void run_diamond_nvs_test(void)
{
    LOGI(TAG, "--- START NVS DIAMOND TEST ---");
    
    kv_handle_t* kv = NULL;
    /* Konfiguracja niezależna od platformy */
    kv_cfg_t cfg = {
        .partition_name = "nvs",      // Mapowane przez adapter na partycję IDF
        .namespace_name = "storage",  
        .read_only = false
    };

    if (kv_open(&cfg, &kv) != PORT_OK) {
        LOGE(TAG, "KV Open failed!");
        return;
    }

    /* Test 1: Typy proste (Integer) */
    int32_t boot_cnt = 0;
    if (kv_get_int(kv, "boot_cnt", &boot_cnt) != PORT_OK) {
        LOGW(TAG, "Pierwsze uruchomienie (nowe urządzenie)");
        boot_cnt = 0;
    } else {
        LOGI(TAG, "Boot count: %ld", (long)boot_cnt);
    }
    boot_cnt++;
    kv_set_int(kv, "boot_cnt", boot_cnt);

    /* Test 2: Struktury binarne (BLOB) - kluczowe dla konfiguracji */
    app_config_blob_t conf = {0};
    size_t blob_len = 0;
    if (kv_get_blob(kv, "sys_cfg", &conf, sizeof(conf), &blob_len) == PORT_OK) {
        LOGI(TAG, "Config loaded. Magic: 0x%08X, Name: %s", (unsigned)conf.boot_magic, conf.device_name);
    } else {
        LOGW(TAG, "Config not found, setting defaults...");
        conf.boot_magic = 0xCAFEBABE;
    }
    
    // Aktualizacja danych
    conf.last_user_id++;
    snprintf(conf.device_name, sizeof(conf.device_name), "ESP_Run_%ld", (long)boot_cnt);
    kv_set_blob(kv, "sys_cfg", &conf, sizeof(conf));

    /* Test 3: Commit i Statystyki */
    kv_commit(kv); // Flush to disk
    
    kv_stats_t st;
    if (kv_get_stats(kv, &st) == PORT_OK) {
        LOGI(TAG, "NVS Health: %u/%u entries used (%u free)", 
             (unsigned)st.used_entries, (unsigned)st.total_entries, (unsigned)st.free_entries);
    }

    kv_close(kv);
    LOGI(TAG, "--- END NVS DIAMOND TEST ---");
}

/* * Composition Root
 * Tutaj, i tylko tutaj, zapada decyzja jak połączyć komponenty.
 */
void app_main(void)
{
    set_default_log_levels();

    // 1. Inicjalizacja Infrastruktury (Low-Level)
    // To jest jedyne miejsce, gdzie main "brudzi sobie ręce" NVS Flash init,
    // ponieważ to wymaganie systemowe ESP-IDF przed użyciem WiFi/BT/PHY.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      LOGW(TAG, "NVS partition corruption detected. Reformatting...");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Weryfikacja pamięci trwałej (Diagnostic)
    run_diamond_nvs_test();

    // 3. Inicjalizacja Rdzenia (Event Bus, Memory Pool)
    ev_init();
    lp_init();
    const ev_bus_t* bus = ev_bus_default();

    // 4. Start Usług (Services) - pracują w tle
    services_timer_start(bus);
    services_i2c_start(bus, 16, 4096, 8); // Queue=16, Stack=4k, Prio=8

    // Serwis UART (Machine-to-Machine interface)
    // Konfiguracja wstrzykiwana (Dependency Injection)
    uart_svc_cfg_t ucfg = {
        .uart_num = 1,
        // Używamy makr, które (w przyszłości) mogą pochodzić z Kconfig
        // Aby to było 100% clean, piny powinny być w sdkconfig.
        .tx_pin = 4, 
        .rx_pin = 5,
        .baud_rate = 115200,
        .pattern_char = '\n' // Frame boundary
    };
    services_uart_start(bus, &ucfg);

    // 5. Start Aktorów (Logic)
    app_log_bus_start(bus);   // Przekierowanie logów na EventBus
    app_demo_lcd_start(bus);  // Główna logika demo

    // 6. Interfejs Diagnostyczny (CLI)
#if CONFIG_INFRA_LOG_CLI
  #if CONFIG_INFRA_LOG_CLI_START_REPL
    infra_log_cli_start_repl();
  #else
    infra_log_cli_register();
  #endif
#endif

    // Main task spełnił swoje zadanie (setup) i może odejść.
    // System działa teraz w pełni asynchronicznie na Event Busie.
    vTaskDelete(NULL);
}

