#include "services_i2c.h"

#include <stdlib.h>
#include <string.h>

#include "core_ev.h"
#include "ports/log_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "I2C_SVC";

typedef struct req_node
{
    i2c_req_t r;
    uint8_t*  tx_copy;  /* właśności usługi */
    uint8_t*  rx_stage; /* staging dla RX */
} req_node_t;

static QueueHandle_t s_q  = NULL;
static TaskHandle_t  s_th = NULL;

static void worker(void* arg)
{
    (void)arg;
    for (;;)
    {
        req_node_t* n = NULL;
        if (xQueueReceive(s_q, &n, portMAX_DELAY) != pdTRUE || !n)
            continue;

        esp_err_t err = ESP_OK;
        switch (n->r.op)
        {
            case I2C_OP_TX:
                err = i2c_tx(n->r.dev, n->tx_copy, n->r.txlen, n->r.timeout_ms);
                break;
            case I2C_OP_RX:
                err = i2c_rx(n->r.dev, n->rx_stage, n->r.rxlen, n->r.timeout_ms);
                if (err == ESP_OK && n->r.rx && n->r.rxlen)
                    memcpy(n->r.rx, n->rx_stage, n->r.rxlen);
                break;
            case I2C_OP_TXRX:
                /* FIX: używaj bufora n->rx_stage (a nie n->r.rx_stage) */
                err = i2c_txrx(n->r.dev, n->tx_copy, n->r.txlen, n->rx_stage, n->r.rxlen, n->r.timeout_ms);
                if (err == ESP_OK && n->r.rx && n->r.rxlen)
                    memcpy(n->r.rx, n->rx_stage, n->r.rxlen);
                break;
            default:
                err = ESP_ERR_INVALID_ARG;
                break;
        }

        /* Event do systemu */
        if (err == ESP_OK)
        {
            ev_post(EV_SRC_I2C, EV_I2C_DONE, (uintptr_t)n->r.user, (uintptr_t)0);
        }
        else
        {
            ev_post(EV_SRC_I2C, EV_I2C_ERROR, (uintptr_t)n->r.user, (uintptr_t)err);
            LOGW(TAG, "I2C op=%d failed: %d", (int)n->r.op, (int)err);
        }

        /* sprzątanie */
        if (n->tx_copy)
            free(n->tx_copy);
        if (n->rx_stage)
            free(n->rx_stage);
        free(n);
    }
}

bool services_i2c_start(int queue_len, int task_stack, int task_prio)
{
    if (s_q)
        return true; /* już działa */
    if (queue_len <= 0)
        queue_len = 16;
    s_q = xQueueCreate(queue_len, sizeof(req_node_t*));
    if (!s_q)
        return false;

    if (xTaskCreate(
            worker, "i2c_svc", task_stack > 0 ? task_stack : 4096, NULL, task_prio > 0 ? task_prio : 8, &s_th) !=
        pdPASS)
    {
        vQueueDelete(s_q);
        s_q = NULL;
        return false;
    }
    return true;
}

bool services_i2c_submit(const i2c_req_t* req)
{
    if (!s_q || !req || !req->dev)
        return false;

    req_node_t* n = (req_node_t*)calloc(1, sizeof(req_node_t));
    if (!n)
        return false;

    n->r = *req; /* płytka kopia metadanych */

    /* TX: kopiujemy do bufora wewnętrznego usługi */
    if (req->tx && req->txlen)
    {
        n->tx_copy = (uint8_t*)malloc(req->txlen);
        if (!n->tx_copy)
        {
            free(n);
            return false;
        }
        memcpy(n->tx_copy, req->tx, req->txlen);
    }

    /* RX: przygotuj staging – po operacji przepiszemy do req->rx */
    if (req->rx && req->rxlen)
    {
        n->rx_stage = (uint8_t*)malloc(req->rxlen);
        if (!n->rx_stage)
        {
            if (n->tx_copy)
                free(n->tx_copy);
            free(n);
            return false;
        }
    }

    if (xQueueSend(s_q, &n, 0) != pdTRUE)
    {
        if (n->tx_copy)
            free(n->tx_copy);
        if (n->rx_stage)
            free(n->rx_stage);
        free(n);
        return false;
    }
    return true;
}
