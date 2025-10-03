#pragma once
#include <stdint.h>
#include "ports/errors.h"
#ifdef __cplusplus
extern "C" { #endif
typedef struct timer_port timer_port_t;
typedef void (*timer_cb_t)(void *user);
typedef struct { timer_cb_t cb; void* user; } timer_cfg_t;
port_err_t timer_create(const timer_cfg_t* cfg, timer_port_t** out);
port_err_t timer_start_oneshot(timer_port_t* t, uint64_t delay_us);
port_err_t timer_cancel(timer_port_t* t);
port_err_t timer_delete(timer_port_t* t);
#ifdef __cplusplus
} #endif
