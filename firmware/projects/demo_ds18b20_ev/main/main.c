/**
 * @file main.c
 * @brief Aplikacja DS18B20 (single-drop) – event-driven i nieblokująca (esp_timer).
 */
#include <stdio.h>

#include "core_ev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services_ds18b20_ev.h"
#include "services_timer.h"

static const char* TAG = "APP_DS";

void app_main(void)
{
    ev_init();
    services_timer_start();

    ds18_svc_cfg_t cfg = {.gpio            = CONFIG_APP_DS_GPIO,
                          .resolution_bits = CONFIG_APP_DS_RES,
                          .period_ms       = CONFIG_APP_DS_PERIOD_MS,
                          .internal_pullup = CONFIG_APP_DS_INT_PULLUP};
    services_ds18_start(&cfg);

    ev_queue_t q;
    ev_subscribe(&q, 16);
    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    ev_msg_t m;
    for (;;)
    {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE)
        {
            if (m.src == EV_SRC_DS18 && m.code == EV_DS18_READY)
            {
                int   milliC = (int)m.a0;
                float C      = milliC / 1000.0f;
                ESP_LOGI(TAG, "Temperatura: %.3f C", C);
            }
            else if (m.src == EV_SRC_DS18 && m.code == EV_DS18_ERROR)
            {
                ESP_LOGW(TAG, "Błąd DS18B20: %ld", (long)m.a0);
            }
        }
    }
}
