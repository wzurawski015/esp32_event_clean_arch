#include "ports/wdt_port.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

/* Mapowanie błędów */
static port_err_t map_err(esp_err_t err) {
    return (err == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t wdt_init(uint32_t timeout_ms) {
    esp_task_wdt_config_t config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = (1 << 0), // Monitoruj też Idle Task na CPU0
        .trigger_panic = true       // Panika = zrzut pamięci na UART przed resetem
    };
    
    // Deinit jest bezpieczny w IDF 5.x jeśli już był zainicjowany
    esp_task_wdt_deinit(); 
    return map_err(esp_task_wdt_init(&config));
}

port_err_t wdt_add_self(void) {
    return map_err(esp_task_wdt_add(NULL)); // NULL = current task
}

port_err_t wdt_reset(void) {
    return map_err(esp_task_wdt_reset());
}

port_err_t wdt_remove_self(void) {
    return map_err(esp_task_wdt_delete(NULL));
}

