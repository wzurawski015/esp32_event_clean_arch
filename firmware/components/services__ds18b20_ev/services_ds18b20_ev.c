#include "services_ds18b20_ev.h"
#include "ports/onewire_port.h"
#include "core/leasepool.h"

#include <string.h>
#include "core_ev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "DS18_SVC";

static onewire_bus_handle_t s_ow = NULL;

/* --- DS18B20 komendy --- */
#define DS_CMD_SKIP_ROM 0xCC
#define DS_CMD_CONVERTT 0x44
#define DS_CMD_RSCRATCH 0xBE

/* --- Serwisowy automat --- */
typedef enum
{
    S_IDLE,
    S_KICK_CONVERT,
    S_WAIT_CONVERT,
    S_READ
} st_t;
static st_t s_st = S_IDLE;

static esp_timer_handle_t s_t_period = NULL;
static esp_timer_handle_t s_t_once   = NULL;

static int s_res_bits  = 12;
static int s_period_ms = 1000;

static int ms_for_res(int bits)
{
    switch (bits)
    {
        case 9:
            return 94;
        case 10:
            return 188;
        case 11:
            return 375;
        default:
            return 750;
    }
}

/* event queue (subskrypcja core__ev) */
static ev_queue_t s_q;
static const ev_bus_t* s_bus = NULL;

/* forward */
static void ds18_task(void* arg);
static void process_sm(void);

static void timer_once_cb(void* arg)
{
    (void)arg;
    /* FIX: Używamy wewnętrznego ticka (NONE), a nie READY (LEASE) */
    ev_bus_post(s_bus, EV_SRC_DS18, EV_DS18_DRV_TICK, 0, 0);
}
static void timer_period_cb(void* arg)
{
    (void)arg;
    if (s_st == S_IDLE)
    {
        s_st = S_KICK_CONVERT;
        /* FIX: Używamy wewnętrznego ticka (NONE) */
        ev_bus_post(s_bus, EV_SRC_DS18, EV_DS18_DRV_TICK, 0, 0);
    }
}

static void start_once(int ms)
{
    if (!s_t_once)
    {
        const esp_timer_create_args_t a = {.callback = timer_once_cb, .name = "ds18_once"};
        esp_err_t                     e = esp_timer_create(&a, &s_t_once);
        (void)e;
    }
    esp_timer_stop(s_t_once);
    esp_timer_start_once(s_t_once, (uint64_t)ms * 1000ULL);
}

static bool ds_start_convert(void)
{
    if (!onewire_reset(s_ow)) return false;
    onewire_write_byte(s_ow, DS_CMD_SKIP_ROM);
    onewire_write_byte(s_ow, DS_CMD_CONVERTT);
    return true;
}

static bool ds_read_temp_raw(int16_t* out_raw)
{
    if (!onewire_reset(s_ow)) return false;
    onewire_write_byte(s_ow, DS_CMD_SKIP_ROM);
    onewire_write_byte(s_ow, DS_CMD_RSCRATCH);
    
    uint8_t scratch[9];
    for(int i=0; i<9; ++i) scratch[i] = onewire_read_byte(s_ow);

    // Industrial Grade: CRC Check
    if (onewire_crc8(scratch, 9) != 0) {
        ESP_LOGE(TAG, "CRC Check Failed!");
        return false;
    }

    uint8_t t_lo = scratch[0];
    uint8_t t_hi = scratch[1];
    *out_raw = (int16_t)((t_hi << 8) | t_lo);
    return true;
}

static void process_sm(void)
{
    switch (s_st)
    {
        case S_IDLE:
            break;

        case S_KICK_CONVERT:
            if (ds_start_convert())
            {
                s_st = S_WAIT_CONVERT;
                start_once(ms_for_res(s_res_bits) + 20);
            }
            else
            {
                ev_bus_post(s_bus, EV_SRC_DS18, EV_DS18_ERROR, 1, 0);
                s_st = S_IDLE;
            }
            break;

        case S_WAIT_CONVERT:
            break;

        case S_READ:
        {
            int16_t raw = 0;
            if (ds_read_temp_raw(&raw))
            {
                /* 12-bit: 1 LSB = 0.0625°C */
                float temp_c = raw * 0.0625f;
                
                lp_handle_t h = lp_alloc_try(sizeof(ds18_result_t));
                if (lp_handle_is_valid(h)) {
                    lp_view_t v; lp_acquire(h, &v);
                    ds18_result_t* r = (ds18_result_t*)v.ptr;
                    r->rom_code = 0; // SKIP_ROM used
                    r->temp_c = temp_c;
                    lp_commit(h, sizeof(ds18_result_t));
                    
                    /* FIX: Tutaj (i tylko tutaj) wysyłamy LEASE */
                    ev_bus_post_lease(s_bus, EV_SRC_DS18, EV_DS18_READY, h, sizeof(ds18_result_t));
                }
            }
            else
            {
                ev_bus_post(s_bus, EV_SRC_DS18, EV_DS18_ERROR, 2, 0);
            }
            s_st = S_IDLE;
        }
        break;

        default:
            s_st = S_IDLE;
            break;
    }
}

static void ds18_task(void* arg)
{
    (void)arg;
    ev_msg_t m;
    for (;;)
    {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) == pdTRUE)
        {
            /* FIX: Reagujemy na wewnętrzny tick, a nie na READY */
            if (m.src == EV_SRC_DS18 && m.code == EV_DS18_DRV_TICK)
            {
                if (s_st == S_WAIT_CONVERT)
                {
                    s_st = S_READ;
                    process_sm();
                }
                else if (s_st == S_KICK_CONVERT)
                {
                    process_sm();
                }
                else if (s_st == S_IDLE)
                {
                    /* heartbeat z periodica */
                    s_st = S_KICK_CONVERT;
                    process_sm();
                }
            }
        }
    }
}

bool services_ds18_start(const ev_bus_t* bus, const ds18_svc_cfg_t* cfg)
{
    if (!cfg) return false;
    if (!bus || !bus->vtbl) return false;
    
    s_bus = bus;
    s_res_bits  = cfg->resolution_bits;
    s_period_ms = cfg->period_ms;

    if (onewire_bus_create(cfg->gpio, &s_ow) != PORT_OK) return false;

    if (!ev_bus_subscribe(s_bus, &s_q, 8)) return false;

    if (!s_t_period)
    {
        const esp_timer_create_args_t a = {.callback = timer_period_cb, .name = "ds18_period"};
        esp_err_t                     e = esp_timer_create(&a, &s_t_period);
        (void)e;
    }
    esp_timer_start_periodic(s_t_period, (uint64_t)s_period_ms * 1000ULL);

    TaskHandle_t th = NULL;
    if (xTaskCreate(ds18_task, "ds18_ev", 4096, NULL, 4, &th) != pdPASS)
        return false;

    ESP_LOGI(TAG, "DS18B20 service started on GPIO%d, res=%db, period=%dms", cfg->gpio, s_res_bits, s_period_ms);
    return true;
}

void services_ds18_stop(void)
{
    if (s_t_period) esp_timer_stop(s_t_period);
    if (s_t_once) esp_timer_stop(s_t_once);
}
