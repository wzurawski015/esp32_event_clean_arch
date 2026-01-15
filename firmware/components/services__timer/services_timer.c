#include "services_timer.h"

#include "sdkconfig.h"

#include <string.h>

#include "core_ev.h"
#include "ports/clock_port.h"
#include "ports/timer_port.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifndef CONFIG_SERVICES_TIMER_MAX_SLOTS
#define CONFIG_SERVICES_TIMER_MAX_SLOTS 16
#endif

// Opcjonalnie: zachowaj stare EV_TICK_* jako periodyczne timery.
#ifndef CONFIG_SERVICES_TIMER_ENABLE_LEGACY_TICKS
#define CONFIG_SERVICES_TIMER_ENABLE_LEGACY_TICKS 0
#endif

static const char* TAG = "SVC_TIMER";

typedef struct
{
    bool active;
    uint16_t gen;
    uint64_t due_us;
    uint64_t period_us;  // 0 = one-shot

    ev_src_t src;
    uint16_t code;
    uint32_t a0;
    uint32_t a1;
} svc_timer_slot_t;

typedef struct
{
    ev_src_t src;
    uint16_t code;
    uint32_t a0;
    uint32_t a1;
} svc_timer_fire_t;

static svc_timer_slot_t s_slots[CONFIG_SERVICES_TIMER_MAX_SLOTS];
static timer_port_t* s_timer = NULL; /* FIX: Poprawny typ (zamiast timer_handle_t) */
static SemaphoreHandle_t s_mu = NULL;

static bool s_started = false;
static bool s_armed   = false;
static uint64_t s_due_us = 0;

static services_timer_token_t token_make_(unsigned idx, uint16_t gen)
{
    return ((services_timer_token_t)gen << 16) | (services_timer_token_t)(idx + 1u);
}

static bool token_parse_(services_timer_token_t tok, unsigned* out_idx, uint16_t* out_gen)
{
    const unsigned lo = (unsigned)(tok & 0xFFFFu);
    if (lo == 0)
        return false;

    const unsigned idx = lo - 1u;
    if (idx >= (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS)
        return false;

    *out_idx = idx;
    *out_gen = (uint16_t)((tok >> 16) & 0xFFFFu);
    return true;
}

static uint64_t periodic_next_due_(uint64_t prev_due_us, uint64_t period_us, uint64_t now_us)
{
    uint64_t next = prev_due_us + period_us;

    // Jeśli jesteśmy daleko w tyle (np. blokady), przeskocz do pierwszego terminu > now.
    if (next <= now_us)
    {
        const uint64_t behind = now_us - next;
        const uint64_t steps  = (behind / period_us) + 1u;
        next += steps * period_us;
    }

    return next;
}

static bool find_earliest_due_locked_(uint64_t* out_due_us)
{
    bool found     = false;
    uint64_t best  = 0;

    for (unsigned i = 0; i < (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS; i++)
    {
        if (!s_slots[i].active)
            continue;
        if (!found || (s_slots[i].due_us < best))
        {
            best  = s_slots[i].due_us;
            found = true;
        }
    }

    if (found)
        *out_due_us = best;
    return found;
}

static void arm_earliest_locked_(uint64_t now_us)
{
    uint64_t next_due_us = 0;

    if (!find_earliest_due_locked_(&next_due_us))
    {
        if (s_armed)
        {
            (void)timer_cancel(s_timer);
            s_armed  = false;
            s_due_us = 0;
        }
        return;
    }

    if (s_armed && (next_due_us == s_due_us))
        return;

    uint64_t delay_us = (next_due_us > now_us) ? (next_due_us - now_us) : 1u;
    if (delay_us == 0)
        delay_us = 1u;

    // Re-arm: cancel + start.
    (void)timer_cancel(s_timer);

    const port_err_t err = timer_start_oneshot(s_timer, delay_us);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "timer_start_oneshot(%llu us) failed: %d", (unsigned long long)delay_us,
                 (int)err);
        s_armed  = false;
        s_due_us = 0;
        return;
    }

    s_armed  = true;
    s_due_us = next_due_us;
}

static void timer_cb_(void* arg)
{
    (void)arg;

    svc_timer_fire_t fires[CONFIG_SERVICES_TIMER_MAX_SLOTS];
    unsigned n_fires = 0;

    const uint64_t now_us = clock_now_us();

    if (!s_mu)
        return;

    xSemaphoreTake(s_mu, portMAX_DELAY);

    // One-shot timer just fired.
    s_armed  = false;
    s_due_us = 0;

    for (unsigned i = 0; i < (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS; i++)
    {
        svc_timer_slot_t* s = &s_slots[i];
        if (!s->active)
            continue;

        if (s->due_us > now_us)
            continue;

        // Emit once per slot.
        if (n_fires < (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS)
        {
            fires[n_fires++] = (svc_timer_fire_t){.src = s->src, .code = s->code, .a0 = s->a0, .a1 = s->a1};
        }

        if (s->period_us == 0)
        {
            s->active = false;
        }
        else
        {
            s->due_us = periodic_next_due_(s->due_us, s->period_us, now_us);
        }
    }

    arm_earliest_locked_(now_us);

    xSemaphoreGive(s_mu);

    // Post outside the mutex.
    for (unsigned i = 0; i < n_fires; i++)
    {
        ev_post(fires[i].src, fires[i].code, fires[i].a0, fires[i].a1);
    }
}

bool services_timer_start(void)
{
    if (s_started)
        return true;

    s_mu = xSemaphoreCreateMutex();
    if (!s_mu)
    {
        ESP_LOGE(TAG, "mutex alloc failed");
        return false;
    }

    memset(s_slots, 0, sizeof(s_slots));
    s_armed   = false;
    s_due_us  = 0;
    s_started = false;

    /* FIX: Użycie poprawnej struktury timer_cfg_t */
    timer_cfg_t cfg = {
        .cb = timer_cb_,
        .user = NULL
    };
    const port_err_t err = timer_create(&cfg, &s_timer);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "timer_create failed: %d", (int)err);
        vSemaphoreDelete(s_mu);
        s_mu = NULL;
        return false;
    }

    s_started = true;

#if CONFIG_SERVICES_TIMER_ENABLE_LEGACY_TICKS
    // Legacy tick generator is implemented on top of the same scheduler.
    (void)services_timer_arm_periodic_us(100000u, EV_SRC_TIMER, EV_TICK_100MS, 0, 0);
    (void)services_timer_arm_periodic_us(1000000u, EV_SRC_TIMER, EV_TICK_1S, 0, 0);
#endif

    return true;
}

void services_timer_stop(void)
{
    if (!s_started)
        return;

    xSemaphoreTake(s_mu, portMAX_DELAY);

    if (s_timer)
    {
        (void)timer_cancel(s_timer);
        (void)timer_delete(s_timer);
        s_timer = NULL;
    }

    memset(s_slots, 0, sizeof(s_slots));
    s_armed   = false;
    s_due_us  = 0;
    s_started = false;

    xSemaphoreGive(s_mu);

    vSemaphoreDelete(s_mu);
    s_mu = NULL;
}

services_timer_token_t services_timer_arm_once_us(uint64_t delay_us, ev_src_t src, uint16_t code,
                                                  uint32_t a0, uint32_t a1)
{
    if (!s_started || !s_mu || !s_timer)
        return 0;

    if (delay_us == 0)
        delay_us = 1u;

    const uint64_t now_us = clock_now_us();

    xSemaphoreTake(s_mu, portMAX_DELAY);

    unsigned slot = (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS;
    for (unsigned i = 0; i < (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS; i++)
    {
        if (!s_slots[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot >= (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS)
    {
        xSemaphoreGive(s_mu);
        ESP_LOGW(TAG, "no free slots (max=%d)", CONFIG_SERVICES_TIMER_MAX_SLOTS);
        return 0;
    }

    svc_timer_slot_t* s = &s_slots[slot];
    uint16_t gen        = (uint16_t)(s->gen + 1u);
    if (gen == 0)
        gen = 1;

    *s = (svc_timer_slot_t){
        .active    = true,
        .gen       = gen,
        .due_us    = now_us + delay_us,
        .period_us = 0,
        .src       = src,
        .code      = code,
        .a0        = a0,
        .a1        = a1,
    };

    arm_earliest_locked_(now_us);
    xSemaphoreGive(s_mu);

    return token_make_(slot, gen);
}

services_timer_token_t services_timer_arm_periodic_us(uint64_t period_us, ev_src_t src, uint16_t code,
                                                      uint32_t a0, uint32_t a1)
{
    if (!s_started || !s_mu || !s_timer)
        return 0;

    if (period_us == 0)
        return 0;

    const uint64_t now_us = clock_now_us();

    xSemaphoreTake(s_mu, portMAX_DELAY);

    unsigned slot = (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS;
    for (unsigned i = 0; i < (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS; i++)
    {
        if (!s_slots[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot >= (unsigned)CONFIG_SERVICES_TIMER_MAX_SLOTS)
    {
        xSemaphoreGive(s_mu);
        ESP_LOGW(TAG, "no free slots (max=%d)", CONFIG_SERVICES_TIMER_MAX_SLOTS);
        return 0;
    }

    svc_timer_slot_t* s = &s_slots[slot];
    uint16_t gen        = (uint16_t)(s->gen + 1u);
    if (gen == 0)
        gen = 1;

    *s = (svc_timer_slot_t){
        .active    = true,
        .gen       = gen,
        .due_us    = now_us + period_us,
        .period_us = period_us,
        .src       = src,
        .code      = code,
        .a0        = a0,
        .a1        = a1,
    };

    arm_earliest_locked_(now_us);
    xSemaphoreGive(s_mu);

    return token_make_(slot, gen);
}

bool services_timer_cancel(services_timer_token_t tok)
{
    if (!s_started || !s_mu)
        return false;

    unsigned idx = 0;
    uint16_t gen = 0;
    if (!token_parse_(tok, &idx, &gen))
        return false;

    const uint64_t now_us = clock_now_us();

    xSemaphoreTake(s_mu, portMAX_DELAY);

    svc_timer_slot_t* s = &s_slots[idx];
    if (!s->active || (s->gen != gen))
    {
        xSemaphoreGive(s_mu);
        return false;
    }

    s->active = false;

    arm_earliest_locked_(now_us);
    xSemaphoreGive(s_mu);

    return true;
}

bool services_timer_is_active(services_timer_token_t tok)
{
    if (!s_started || !s_mu)
        return false;

    unsigned idx = 0;
    uint16_t gen = 0;
    if (!token_parse_(tok, &idx, &gen))
        return false;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    const bool ok = s_slots[idx].active && (s_slots[idx].gen == gen);
    xSemaphoreGive(s_mu);

    return ok;
}

