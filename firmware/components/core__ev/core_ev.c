#include "core_ev.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct
{
    ev_queue_t q;
} sub_t;

static sub_t             s_subs[EV_MAX_SUBS];
static SemaphoreHandle_t s_lock;

void ev_init(void)
{
    memset(s_subs, 0, sizeof(s_subs));
    s_lock = xSemaphoreCreateMutex();
}

static bool add_sub_locked(ev_queue_t q)
{
    for (size_t i = 0; i < EV_MAX_SUBS; i++)
    {
        if (s_subs[i].q == NULL)
        {
            s_subs[i].q = q;
            return true;
        }
    }
    return false;
}

bool ev_subscribe(ev_queue_t* out_queue, size_t queue_len)
{
    if (!out_queue)
        return false;
    ev_queue_t q = xQueueCreate(queue_len, sizeof(ev_msg_t));
    if (!q)
        return false;
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ok = add_sub_locked(q);
    xSemaphoreGive(s_lock);
    if (!ok)
        vQueueDelete(q);
    else
        *out_queue = q;
    return ok;
}

bool ev_register_queue(ev_queue_t q)
{
    if (!q)
        return false;
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ok = add_sub_locked(q);
    xSemaphoreGive(s_lock);
    return ok;
}

bool ev_post(uint16_t src, uint16_t code, uintptr_t a0, uintptr_t a1)
{
    ev_msg_t m = {
        .src  = src,
        .code = code,
        .a0   = a0,
        .a1   = a1,
        .t_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
    };
    bool any = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < EV_MAX_SUBS; i++)
    {
        if (s_subs[i].q)
        {
            xQueueSend(s_subs[i].q, &m, 0);
            any = true;
        }
    }
    xSemaphoreGive(s_lock);
    return any;
}
