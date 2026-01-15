#include "sdkconfig.h"
#include "esp_log.h"

#include "core_ev.h"
#include "core/leasepool.h"

#include "services_timer.h"
#include "services_i2c.h"
#include "services_uart.h"   // +++

#include "app_log_bus.h"
#include "app_demo_lcd.h"

#if CONFIG_INFRA_LOG_CLI
#include "logging_cli.h"
#endif

static void set_default_log_levels(void)
{
    esp_log_level_set("*",       ESP_LOG_INFO);
    esp_log_level_set("APP",     ESP_LOG_WARN);
    esp_log_level_set("LOGCLI",  ESP_LOG_INFO);
    esp_log_level_set("DFR_LCD", ESP_LOG_DEBUG);
    esp_log_level_set("SVC_UART", ESP_LOG_INFO); // +++
}

void app_main(void)
{
    set_default_log_levels();

    ev_init();
    lp_init();

    const ev_bus_t* bus = ev_bus_default();

    // 1. Timery i I2C
    services_timer_start(bus);
    services_i2c_start(bus, 16, 4096, 8);

    // 2. Logi
    app_log_bus_start(bus);

    // 3. UART Service (M2M) na UART1
    //    Uwaga: Piny 4/5 dla C6 (przykładowo), dostosuj do płytki!
    uart_svc_cfg_t ucfg = {
        .uart_num = 1,
        .tx_pin = 4,
        .rx_pin = 5,
        .baud_rate = 115200,
        .pattern_char = '\n' // Wykrywaj koniec linii
    };
    services_uart_start(bus, &ucfg);

    // 4. Aktorzy
    app_demo_lcd_start(bus);

    // 5. CLI
#if CONFIG_INFRA_LOG_CLI
  #if CONFIG_INFRA_LOG_CLI_START_REPL
    infra_log_cli_start_repl();
  #else
    infra_log_cli_register();
  #endif
#endif

    vTaskDelete(NULL);
}
