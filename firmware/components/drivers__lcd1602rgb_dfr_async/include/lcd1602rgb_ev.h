#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core_ev.h"

#ifdef __cplusplus
extern "C" {
#endif

// PR2: EV_LCD_CMD_* są zdefiniowane centralnie w core_ev_schema.h (single source of truth)

// Pakowanie RGB do a0 (R | G<<8 | B<<16)
static inline uint32_t lcd_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
}
static inline void lcd_unpack_rgb(uint32_t w, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (r) *r = (uint8_t)(w & 0xFF);
    if (g) *g = (uint8_t)((w >> 8) & 0xFF);
    if (b) *b = (uint8_t)((w >> 16) & 0xFF);
}

// Nagłówek w LEASE dla DRAW_ROW: [row(0/1),3xpad] + tekst...
typedef struct __attribute__((packed)) {
    uint8_t row;
    uint8_t _pad[3];
} lcd_cmd_draw_row_hdr_t;

bool lcd1602rgb_ev_start(void);

#ifdef __cplusplus
}
#endif
