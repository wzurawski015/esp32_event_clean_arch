#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core_ev.h"

#include <string.h>

static ev_queue_t s_hi_q = NULL;
static ev_queue_t s_lo_q = NULL;

static volatile bool s_hi_ok = false;
static volatile bool s_lo_ok = false;

static void task_hi(void *arg)
{
    TaskHandle_t parent = (TaskHandle_t)arg;

    ev_msg_t m = {0};
    if (xQueueReceive(s_hi_q, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lp_handle_t h = lp_unpack_handle_u32(m.a0);

        lp_view_t v = {0};
        const bool ok = lp_acquire(h, &v);
        if (ok && v.ptr && v.len >= 1) {
            volatile uint8_t b = ((const uint8_t *)v.ptr)[0];
            (void)b;
        }

        // Zawsze zwalniamy (to "nasz" ref od ev_broadcast_lease), nawet jeśli ok==false.
        lp_release(h);
        s_hi_ok = ok;
    }

    xTaskNotifyGive(parent);
    vTaskDelete(NULL);
}

static void task_lo(void *arg)
{
    TaskHandle_t parent = (TaskHandle_t)arg;

    ev_msg_t m = {0};
    if (xQueueReceive(s_lo_q, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lp_handle_t h = lp_unpack_handle_u32(m.a0);

        lp_view_t v = {0};
        const bool ok = lp_acquire(h, &v);
        if (ok && v.ptr && v.len == 4) {
            s_lo_ok = (memcmp(v.ptr, "TEST", 4) == 0);
        } else {
            s_lo_ok = false;
        }

        lp_release(h);
    }

    xTaskNotifyGive(parent);
    vTaskDelete(NULL);
}

TEST_CASE("ev_post_lease: retains across broadcast (deterministic)", "[core_ev]")
{
    lp_init();
    ev_init();

    // Dwie subskrypcje (kolejki) na ten sam event LEASE.
    TEST_ASSERT_TRUE(ev_subscribe(&s_hi_q, 2));
    TEST_ASSERT_TRUE(ev_subscribe(&s_lo_q, 2));

    // Task HI ma wyższy priorytet i powinien preemptować w trakcie broadcastu.
    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    s_hi_ok = false;
    s_lo_ok = false;

    TaskHandle_t hi = NULL;
    TaskHandle_t lo = NULL;

    (void)xTaskCreate(task_hi, "hi", 2048, self, configMAX_PRIORITIES - 1, &hi);
    (void)xTaskCreate(task_lo, "lo", 2048, self, configMAX_PRIORITIES - 2, &lo);

    // Tworzymy payload LEASE.
    lp_handle_t h = lp_alloc_try(4);
    TEST_ASSERT_TRUE(lp_handle_is_valid(h));

    lp_view_t v = {0};
    TEST_ASSERT_TRUE(lp_acquire(h, &v));
    TEST_ASSERT_TRUE(v.ptr != NULL);
    TEST_ASSERT_TRUE(v.cap >= 4);

    memcpy(v.ptr, "TEST", 4);
    lp_commit(h, 4);

    // Najważniejsze: ev_post_lease MUSI dodać refcount przed enqueue.
    // W przeciwnym razie task_hi może zwolnić slot zanim task_lo go odbierze.
    TEST_ASSERT_TRUE(ev_post_lease(EV_SRC_LOG, EV_LOG_NEW, h, 4));

    // Dajemy szansę na natychmiastową preempcję.
    taskYIELD();

    // Czekamy aż obie taski skończą.
    TEST_ASSERT_TRUE(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) > 0);
    TEST_ASSERT_TRUE(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) > 0);

    TEST_ASSERT_TRUE(s_hi_ok);
    TEST_ASSERT_TRUE(s_lo_ok);

    // Porządek: po zwolnieniu przez 2 subskrybentów pool powinien wrócić do pełnej "wolności".
    lp_stats_t st = {0};
    lp_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT16(st.slots_total, st.slots_free);

    // Cleanup (w testach IDF to nie jest krytyczne, ale utrzymuje izolację).
    ev_unsubscribe(s_hi_q);
    ev_unsubscribe(s_lo_q);
    vQueueDelete(s_hi_q);
    vQueueDelete(s_lo_q);
    s_hi_q = NULL;
    s_lo_q = NULL;
}
