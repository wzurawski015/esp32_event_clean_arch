#include "sdkconfig.h"
#include "esp_log.h"

#include "core_ev.h"
#include "core/leasepool.h"

#include "services_timer.h"
#include "services_i2c.h"

#include "app_log_bus.h"     // log -> EV (STREAM/READY)
#include "app_demo_lcd.h"    // aktor LCD

#if CONFIG_INFRA_LOG_CLI
#include "logging_cli.h"
#endif

static void set_default_log_levels(void)
{
    esp_log_level_set("*",       ESP_LOG_INFO);
    esp_log_level_set("APP",     ESP_LOG_WARN);
    esp_log_level_set("LOGCLI",  ESP_LOG_INFO);
    esp_log_level_set("DFR_LCD", ESP_LOG_DEBUG);
    esp_log_level_set("APP_DEMO_LCD", ESP_LOG_INFO);
}

void app_main(void)
{
    // 0) Poziomy logów – ergonomia od pierwszego bootu
    set_default_log_levels();

    // 1) Rdzeń: event-bus + pula buforów (lease)
    ev_init();
    lp_init();

    // 2) Serwisy systemowe
    services_timer_start();
    services_i2c_start(16, 4096, 8);

    // 3) Most log->EV (STREAM): każda nowa linia logu = EV_LOG_READY, payload w ringu
    app_log_bus_start();

    // 4) Aplikacja jako aktor (LCD, w razie potrzeby inne)
    app_demo_lcd_start();

    // 5) CLI (opcjonalnie)
#if CONFIG_INFRA_LOG_CLI
  #if CONFIG_INFRA_LOG_CLI_START_REPL
    infra_log_cli_start_repl();
  #else
    infra_log_cli_register();
  #endif
#endif

    // 6) Main kończy się – żyją aktorzy/serwisy
    vTaskDelete(NULL);
}
