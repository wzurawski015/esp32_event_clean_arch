#include "app_log_bus.h"
#include "ports/log_port.h"

static const char* TAG = "APP_LOG_BUS";

bool app_log_bus_start(void)
{
    LOGI(TAG, "Log-bus ready (stub). Hook do loggera dodamy w kolejnym kroku.");
    return true;
}
