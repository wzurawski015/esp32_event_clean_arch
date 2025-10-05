#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" { 
#endif

// Uruchamia aktora „LCD demo”: skanuje I²C, inicjuje LCD i startuje pętlę zdarzeń.
bool app_demo_lcd_start(void);

#ifdef __cplusplus
}
#endif
