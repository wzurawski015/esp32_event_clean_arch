/**
 * @file main.c
 * @brief Minimalny przykład: event-bus + EV_TICK_1S/100MS + log.
 */
#include "core_ev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services_timer.h"

static const char* TAG = "APP";

void app_main(void)
{
    ev_init();
    services_timer_start();

    ev_queue_t q;
    ev_subscribe(&q, 16);

    ESP_LOGI(TAG, "Start aplikacji (event-driven)");

    ev_post(EV_SRC_SYS, EV_SYS_START, 0, 0);

    ev_msg_t m;
    for (;;)
    {
        if (xQueueReceive(q, &m, portMAX_DELAY) == pdTRUE)
        {
            if (m.src == EV_SRC_TIMER && m.code == EV_TICK_1S)
            {
                ESP_LOGI(TAG, "[%u ms] Ping EV_TICK_1S", (unsigned)m.t_ms);
            }
        }
    }
}
