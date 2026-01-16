#include "services_uart.h"
#include "ports/uart_port.h"
#include "ports/wdt_port.h"  // <--- NOWOŚĆ
#include "core_ev.h"
#include "core/leasepool.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char* TAG = "SVC_UART";

typedef struct {
    int type;
    size_t size;
} uart_evt_t;

static uart_port_handle_t s_port = NULL;
static const ev_bus_t* s_bus = NULL;
static QueueSetHandle_t s_qset = NULL;
static ev_queue_t s_tx_sub_q = NULL;
static TaskHandle_t s_task = NULL;

static void handle_rx_event(uart_evt_t* evt)
{
    size_t len = 0;

    if (uart_port_is_pattern_event(evt->type)) {
        int pos = uart_port_pop_pattern(s_port);
        if (pos != -1) {
            len = pos + 1;
        }
    } else if (uart_port_is_data_event(evt->type)) {
        len = evt->size;
    } else {
        if (evt->type != 0) {
            ESP_LOGW(TAG, "UART HW event type: %d", evt->type);
        }
        return;
    }

    if (len == 0) return;

    lp_handle_t h = lp_alloc_try((uint32_t)len + 1);
    if (!lp_handle_is_valid(h)) {
        ESP_LOGE(TAG, "RX Drop: LeasePool full (%u bytes)", (unsigned)len);
        uint8_t trash[64];
        size_t to_read = len;
        while(to_read > 0) {
            int r = uart_port_read(s_port, trash, (to_read > sizeof(trash)) ? sizeof(trash) : to_read, 0);
            if (r <= 0) break;
            to_read -= r;
        }
        return;
    }

    lp_view_t v;
    if (lp_acquire(h, &v)) {
        int read = uart_port_read(s_port, v.ptr, len, 100);
        if (read > 0) {
            ((uint8_t*)v.ptr)[read] = 0;
            lp_commit(h, read);
            
            // Wysyłamy LEASE z poprawnym źródłem
            ev_bus_post_lease(s_bus, EV_SRC_UART, EV_UART_FRAME, h, read);
            
        } else {
            lp_release(h);
        }
    } else {
        lp_release(h);
    }
}

static void handle_tx_request(ev_msg_t* m)
{
    lp_handle_t h = lp_unpack_handle_u32(m->a0);
    lp_view_t v;

    if (lp_acquire(h, &v)) {
        if (v.ptr && v.len > 0) {
            uart_port_write(s_port, v.ptr, v.len);
        }
        lp_release(h);
    } else {
        ESP_LOGW(TAG, "TX Req: Invalid lease handle");
    }
}

static void uart_worker_task(void* arg)
{
    (void)arg;
    QueueHandle_t active_q;

    // 1. Rejestracja w Watchdogu (Sentinel)
    wdt_add_self();

    while (1) {
        // 2. Heartbeat Loop: Czekaj max 1000ms.
        // Jeśli przez sekundę nie ma zdarzeń UART (cisza na linii),
        // funkcja zwróci NULL, a my zresetujemy Watchdoga.
        active_q = xQueueSelectFromSet(s_qset, pdMS_TO_TICKS(1000));

        if (active_q == NULL) {
            wdt_reset(); // IDLE state: "Żyję, tylko nie ma danych"
            continue;
        }

        // 3. Przyszło zdarzenie (RX lub TX) -> Resetujemy WDT przed pracą
        wdt_reset();

        if (active_q == uart_port_get_event_queue(s_port)) {
            uart_evt_t evt;
            if (xQueueReceive(active_q, &evt, 0)) {
                handle_rx_event(&evt);
            }
        }
        else if (active_q == s_tx_sub_q) {
            ev_msg_t msg;
            if (xQueueReceive(active_q, &msg, 0)) {
                if (msg.code == EV_UART_TX_REQ) {
                    handle_tx_request(&msg);
                }
            }
        }
        
        // 4. Reset po pracy (dla pewności, przy długich ramkach)
        wdt_reset();
    }
    
    // Sprzątanie (teoretycznie nieosiągalne)
    wdt_remove_self();
    vTaskDelete(NULL);
}

bool services_uart_start(const ev_bus_t* bus, const uart_svc_cfg_t* cfg)
{
    if (!bus || !cfg) return false;
    if (s_port) return true;

    s_bus = bus;

    uart_cfg_t pcfg = {
        .uart_num = cfg->uart_num,
        .tx_pin = cfg->tx_pin,
        .rx_pin = cfg->rx_pin,
        .baud_rate = cfg->baud_rate,
        .rx_buf_size = 1024,
        .tx_buf_size = 1024
    };

    if (uart_port_create(&pcfg, &s_port) != PORT_OK) {
        ESP_LOGE(TAG, "Failed to create UART port");
        return false;
    }

    if (cfg->pattern_char != 0) {
        uart_port_enable_pattern_det(s_port, cfg->pattern_char);
    }

    if (!ev_bus_subscribe(s_bus, &s_tx_sub_q, 8)) {
        ESP_LOGE(TAG, "Failed to subscribe to EV bus");
        return false;
    }

    s_qset = xQueueCreateSet(16 + 20);
    QueueHandle_t uart_q = uart_port_get_event_queue(s_port);

    if (!s_qset || !uart_q) {
        ESP_LOGE(TAG, "QueueSet error");
        return false;
    }

    xQueueAddToSet(uart_q, s_qset);
    xQueueAddToSet(s_tx_sub_q, s_qset);

    if (xTaskCreate(uart_worker_task, "svc_uart", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed");
        return false;
    }

    ESP_LOGI(TAG, "Service started. Pattern: 0x%02X", cfg->pattern_char);
    return true;
}

