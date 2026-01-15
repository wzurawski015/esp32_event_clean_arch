/**
 * @file main.c
 * @brief Aplikacja DS18B20 (single-drop) – event-driven i nieblokująca (esp_timer).
 */
#include <stdio.h>

#include "core_ev.h"
#include "ports/log_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services_ds18b20_ev.h"
#include "services_timer.h"
#include "core/leasepool.h"

static const char* TAG = "APP_DS";

void app_main(void)
{
    ev_init();
    lp_init(); // Ważne!

    const ev_bus_t* bus = ev_bus_default();
    services_timer_start(bus);

    ds18_svc_cfg_t cfg = {.gpio            = CONFIG_APP_DS_GPIO,
                          .resolution_bits = CONFIG_APP_DS_RES,
                          .period_ms       = CONFIG_APP_DS_PERIOD_MS};
    services_ds18_start(bus, &cfg);

    ev_queue_t q;
    ev_bus_subscribe(bus, &q, 16);
    ev_bus_post(bus, EV_SRC_SYS, EV_SYS_START, 0, 0);

    ev_msg_t m;
    for (;;)
    {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE)
        {
            if (m.src == EV_SRC_DS18 && m.code == EV_DS18_READY)
            {
                // Nowa obsługa: Payload jest w LeasePool (struktura ds18_result_t)
                lp_handle_t h = lp_unpack_handle_u32(m.a0);
                lp_view_t v;
                if (lp_acquire(h, &v)) {
                    if (v.len == sizeof(ds18_result_t)) {
                        const ds18_result_t* r = (const ds18_result_t*)v.ptr;
                        LOGI(TAG, "Temperatura: %.2f C (ROM: %llX)", r->temp_c, r->rom_code);
                    }
                    lp_release(h);
                } else {
                    // Fallback dla wewnętrznych "ticków" serwisu (jeśli nie używają payloadu)
                    // W tej implementacji ticki są "puste" w a0, więc h byłoby invalid.
                }
            }
            else if (m.src == EV_SRC_DS18 && m.code == EV_DS18_ERROR)
            {
                LOGW(TAG, "Błąd DS18B20: %ld", (long)m.a0);
            }
        }
    }
}
