#pragma once
#include <stdbool.h>

#include "core_ev.h"

#ifdef __cplusplus
extern "C" { 
#endif

// Uruchamia aktora „LCD demo”: skanuje I²C, inicjuje LCD i startuje pętlę zdarzeń.
bool app_demo_lcd_start(const ev_bus_t* bus);

#ifdef __cplusplus
}
#endif
