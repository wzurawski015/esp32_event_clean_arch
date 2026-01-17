#include "services_led.h"
#include "ports/led_strip_port.h"
#include "ports/wdt_port.h"
#include "ports/log_port.h"
#include "core_ev.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char* TAG = "SVC_LED";

static led_strip_dev_t* s_strip = NULL;
static const ev_bus_t* s_bus   = NULL;
static ev_queue_t       s_q     = NULL;
static TaskHandle_t     s_task  = NULL;

static void unpack_rgb(uint32_t packed, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)(packed & 0xFF);
    *g = (uint8_t)((packed >> 8) & 0xFF);
    *b = (uint8_t)((packed >> 16) & 0xFF);
}

static void led_task(void* arg)
{
    (void)arg;
    ev_msg_t msg;

    if (wdt_add_self() != PORT_OK) {
        LOGE(TAG, "Failed to register in WDT!");
    }

    for (;;) {
        if (xQueueReceive(s_q, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            wdt_reset();

            if (msg.code == EV_LED_SET_RGB) {
                uint8_t r, g, b;
                unpack_rgb(msg.a0, &r, &g, &b);
                
                // Używamy nowych nazw z portu (led_port_*)
                led_port_set_pixel(s_strip, 0, r, g, b);
                
                if (led_port_refresh(s_strip, 100) != PORT_OK) {
                    LOGW(TAG, "LED refresh failed");
                }
            }
            else if (msg.code == EV_SYS_START) {
                led_port_set_pixel(s_strip, 0, 0, 0, 20); // Blue startup
                led_port_refresh(s_strip, 100);
            }
        } else {
            wdt_reset();
        }
    }

    wdt_remove_self();
    vTaskDelete(NULL);
}

bool services_led_start(const ev_bus_t* bus, const led_svc_cfg_t* cfg)
{
    if (!bus || !cfg) return false;
    if (s_task) return true;

    s_bus = bus;

    led_strip_cfg_t port_cfg = {
        .gpio_num = cfg->gpio_num,
        .max_leds = cfg->max_leds,
        .type     = (led_type_t)cfg->led_type,
        .use_dma  = true 
    };

    if (led_port_create(&port_cfg, &s_strip) != PORT_OK) {
        LOGE(TAG, "Port create failed");
        return false;
    }

    if (!ev_bus_subscribe(s_bus, &s_q, 8)) {
        LOGE(TAG, "Subscribe failed");
        led_port_delete(s_strip);
        return false;
    }

    BaseType_t ret = xTaskCreate(led_task, "svc_led", 3072, NULL, tskIDLE_PRIORITY + 1, &s_task);
    if (ret != pdPASS) {
        LOGE(TAG, "Task create failed");
        // FIX: ev_bus_unsubscribe wymaga 2 argumentów
        ev_bus_unsubscribe(s_bus, s_q); 
        vQueueDelete(s_q);
        led_port_delete(s_strip);
        return false;
    }

    LOGI(TAG, "Service started on GPIO%d", cfg->gpio_num);
    return true;
}

void services_led_stop(void)
{
    // Placeholder
}
