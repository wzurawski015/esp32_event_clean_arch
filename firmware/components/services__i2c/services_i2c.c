/**
 * @file services_i2c.c
 * @brief Worker I²C – wykonuje żądania w tle i publikuje EV_I2C_DONE z esp_err_t.
 */
#include "services_i2c.h"
#include "core_ev.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "I2C_SVC";

typedef struct {
    i2c_req_t req;
} qitem_t;

static QueueHandle_t s_q = NULL;

static void i2c_worker(void* arg)
{
    (void)arg;
    qitem_t item;
    for (;;) {
        if (xQueueReceive(s_q, &item, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = ESP_FAIL;
            switch (item.req.op) {
                case I2C_OP_TX:   err = i2c_tx(item.req.dev, item.req.tx, item.req.txlen, item.req.timeout_ms); break;
                case I2C_OP_RX:   err = i2c_rx(item.req.dev, item.req.rx, item.req.rxlen, item.req.timeout_ms); break;
                case I2C_OP_TXRX: err = i2c_txrx(item.req.dev, item.req.tx, item.req.txlen, item.req.rx, item.req.rxlen, item.req.timeout_ms); break;
                default:          err = ESP_ERR_INVALID_ARG; break;
            }
            /* data1 = esp_err_t (cast), data2 = user context (opcjonalnie) */
            ev_post(EV_SRC_I2C, EV_I2C_DONE, (uintptr_t)err, (uintptr_t)item.req.user);
        }
    }
}

bool services_i2c_start(size_t queue_len, uint32_t worker_stack, UBaseType_t worker_prio)
{
    if (s_q) return true;
    s_q = xQueueCreate(queue_len, sizeof(qitem_t));
    if (!s_q) return false;

    TaskHandle_t th = NULL;
    return xTaskCreate(i2c_worker, "i2c_svc", worker_stack, NULL, worker_prio, &th) == pdPASS;
}

bool services_i2c_submit(const i2c_req_t* req)
{
    if (!s_q || !req) return false;
    qitem_t item = { .req = *req };
    return xQueueSend(s_q, &item, 0) == pdTRUE;
}
