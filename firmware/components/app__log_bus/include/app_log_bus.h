#pragma once
#include <stdbool.h>

#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start mostka log->EV (wrapuje esp_log_set_vprintf i puszcza EV_LOG_READY jako STREAM).
bool app_log_bus_start(const ev_bus_t* bus);

#ifdef __cplusplus
}
#endif
