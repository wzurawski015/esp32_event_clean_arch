#include "unity.h"
#include "unity_test_runner.h"

#include "core_ev.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

TEST_CASE("ev_post: REPLACE_LAST overwrites on depth=1 queue (no backlog)", "[core__ev]")
{
    ev_init();

    ev_queue_t q = NULL;
    TEST_ASSERT_TRUE(ev_subscribe(&q, 1));

    const uint32_t rgb1 = 0x00010203u;
    const uint32_t rgb2 = 0x000A0B0Cu;

    TEST_ASSERT_TRUE(ev_post(EV_SRC_LCD, EV_LCD_CMD_SET_RGB, rgb1, 0));
    TEST_ASSERT_TRUE(ev_post(EV_SRC_LCD, EV_LCD_CMD_SET_RGB, rgb2, 0));

    ev_msg_t m = {0};
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &m, pdMS_TO_TICKS(100)));

    TEST_ASSERT_EQUAL_UINT16(EV_SRC_LCD, m.src);
    TEST_ASSERT_EQUAL_UINT16(EV_LCD_CMD_SET_RGB, m.code);
    TEST_ASSERT_EQUAL_UINT32(rgb2, m.a0);

    // Queue powinien być pusty (1-slot + overwrite)
    TEST_ASSERT_EQUAL(pdFALSE, xQueueReceive(q, &m, 0));

    // Global stats: bez overwrite drugi post skończyłby się enq_fail.
    ev_stats_t s = {0};
    ev_get_stats(&s);
    TEST_ASSERT_EQUAL_UINT32(0u, s.enq_fail);

    // Per-event stats: sprawdź, że licznik ok==2 i delivered==2.
    const size_t n = ev_meta_count();
    TEST_ASSERT_TRUE(n > 0);

    size_t idx = n;
    for (size_t i = 0; i < n; i++) {
        const ev_meta_t* meta = ev_meta_by_index(i);
        if (meta && meta->src == EV_SRC_LCD && meta->code == EV_LCD_CMD_SET_RGB) {
            idx = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(idx < n);

    ev_event_stats_t* es = (ev_event_stats_t*)calloc(n, sizeof(*es));
    TEST_ASSERT_NOT_NULL(es);

    const size_t got = ev_get_event_stats(es, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)got);

    TEST_ASSERT_EQUAL_UINT32(2u, es[idx].posts_ok);
    TEST_ASSERT_EQUAL_UINT32(0u, es[idx].posts_drop);
    TEST_ASSERT_EQUAL_UINT32(0u, es[idx].enq_fail);
    TEST_ASSERT_EQUAL_UINT32(2u, es[idx].delivered);

    free(es);

    ev_unsubscribe(q);
    vQueueDelete(q);
}
