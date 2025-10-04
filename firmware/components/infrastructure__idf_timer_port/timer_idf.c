#include "ports/timer_port.h"
#include "esp_timer.h"
#include <stdlib.h>

struct timer_port {
    esp_timer_handle_t h;
    timer_cfg_t        cfg;
};

static void _thunk(void* arg)
{
    struct timer_port* t = (struct timer_port*)arg;
    if (t->cfg.cb) {
        t->cfg.cb(t->cfg.user);
    }
}

port_err_t timer_create(const timer_cfg_t* cfg, timer_port_t** out)
{
    if (!cfg || !out || !cfg->cb) {
        return PORT_ERR_INVALID_ARG;
    }

    struct timer_port* t = (struct timer_port*)calloc(1, sizeof(*t));
    if (!t) {
        return PORT_FAIL;
    }
    t->cfg = *cfg;

    const esp_timer_create_args_t args = {
        .callback        = _thunk,
        .arg             = t,
        .name            = "app_timer",
        .dispatch_method = ESP_TIMER_TASK,  // callback w kontekście taska (nie ISR)
    };

    esp_err_t e = esp_timer_create(&args, &t->h);
    if (e != ESP_OK) {
        free(t);
        return e; // pod IDF: port_err_t == esp_err_t (mapowanie w ports/errors.h)
    }

    *out = t;
    return ESP_OK;
}

port_err_t timer_start_oneshot(timer_port_t* t, uint64_t delay_us)
{
    if (!t) return PORT_ERR_INVALID_ARG;
    return esp_timer_start_once(t->h, delay_us);
}

port_err_t timer_cancel(timer_port_t* t)
{
    if (!t) return PORT_ERR_INVALID_ARG;
    return esp_timer_stop(t->h);
}

port_err_t timer_delete(timer_port_t* t)
{
    if (!t) return ESP_OK;
    esp_err_t e = esp_timer_delete(t->h);
    free(t);
    return e;
}
