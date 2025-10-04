#include "ports/clock_port.h"
#include "esp_timer.h"

uint64_t clock_now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}
