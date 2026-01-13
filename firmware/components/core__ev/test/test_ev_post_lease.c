#include "unity.h"
#include "unity_test_runner.h"

#include "core_ev.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static ev_queue_t s_q_hi = NULL;
static ev_queue_t s_q_lo = NULL;

static volatile bool s_hi_ok = false;
static volatile bool s_lo_ok = false;

static void task_hi(void* arg)
{
    ev_msg_t m = {0};

    if (xQueueReceive(s_q_hi, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lp_handle_t h = lp_unpack_handle_u32(m.a0);

        void* ptr = NULL;
        uint16_t cap = 0;
        uint16_t len = 0;

        bool ok = lp_acquire(h, &ptr, &cap, &len);
        if (ok && ptr && len > 0) {
            volatile uint8_t b = ((uint8_t*)ptr)[0];
            (void)b;
        }

        lp_release(h);
        s_hi_ok = ok;
    } else {
        s_hi_ok = false;
    }

    xTaskNotifyGive((TaskHandle_t)arg);
    vTaskDelete(NULL);
}

static void task_lo(void* arg)
{
    ev_msg_t m = {0};

    if (xQueueReceive(s_q_lo, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lp_handle_t h = lp_unpack_handle_u32(m.a0);

        void* ptr = NULL;
        uint16_t cap = 0;
        uint16_t len = 0;

        bool ok = lp_acquire(h, &ptr, &cap, &len);
        if (ok && ptr && len >= 4) {
            const char* s = (const char*)ptr;
            s_lo_ok = (s[0] == 'T' && s[1] == 'E' && s[2] == 'S' && s[3] == 'T');
        } else {
            s_lo_ok = false;
        }

        lp_release(h);
    } else {
        s_lo_ok = false;
    }

    xTaskNotifyGive((TaskHandle_t)arg);
    vTaskDelete(NULL);
}

TEST_CASE("ev_post_lease: refcount reserved before enqueue (no UAF)", "[core__ev]")
{
    lp_init();
    ev_init();

    TEST_ASSERT_TRUE(ev_subscribe(&s_q_hi, 1));
    TEST_ASSERT_TRUE(ev_subscribe(&s_q_lo, 1));

    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    // wymuś preempcję: hi > lo > test
    const UBaseType_t hi_prio = (configMAX_PRIORITIES > 2) ? (configMAX_PRIORITIES - 1) : 2;
    const UBaseType_t lo_prio = (configMAX_PRIORITIES > 3) ? (configMAX_PRIORITIES - 2) : 1;

    xTaskCreate(task_hi, "ev_hi", 3072, self, hi_prio, NULL);
    xTaskCreate(task_lo, "ev_lo", 3072, self, lo_prio, NULL);

    vTaskDelay(pdMS_TO_TICKS(10));

    lp_handle_t h = lp_alloc_try();
    TEST_ASSERT_NOT_EQUAL(0u, (uint32_t)h);

    void* ptr = NULL;
    uint16_t cap = 0;
    uint16_t len0 = 0;
    TEST_ASSERT_TRUE(lp_acquire(h, &ptr, &cap, &len0));
    TEST_ASSERT_TRUE(cap >= 4);

    ((char*)ptr)[0] = 'T';
    ((char*)ptr)[1] = 'E';
    ((char*)ptr)[2] = 'S';
    ((char*)ptr)[3] = 'T';
    lp_commit(h, 4);

    (void)ev_post_lease(EV_SRC_LOG, EV_LOG_NEW, h, 4);

    // PANCERNE czekanie: oba taski mogą zdążyć „oddać” notify zanim zaczniemy brać.
    // ulTaskNotifyTake(pdTRUE, ...) zwraca licznik (>=1) i czyści go do 0.
    uint32_t total = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(1000);

    while (total < 2u) {
        const TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            break;
        }

        total += ulTaskNotifyTake(pdTRUE, (deadline - now));
    }

    TEST_ASSERT_EQUAL_UINT32(2u, total);

    TEST_ASSERT_TRUE(s_hi_ok);
    TEST_ASSERT_TRUE(s_lo_ok);

    ev_unsubscribe(s_q_hi);
    ev_unsubscribe(s_q_lo);
    vQueueDelete(s_q_hi);
    vQueueDelete(s_q_lo);
}

