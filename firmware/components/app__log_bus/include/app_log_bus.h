#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start mostka log->EV (wrapuje esp_log_set_vprintf i puszcza EV_LOG_READY jako STREAM).
bool app_log_bus_start(void);

#ifdef __cplusplus
}
#endif
