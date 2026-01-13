#pragma once

// PR2: Event schema (single source of truth) jako X-macro.
//
// UWAGA: Nie includuj tego pliku bezposrednio.
// Zawsze includuj najpierw "core_ev.h" (bo potrzebujesz EV_SRC_*).
//
// Format: X(NAME, SRC, CODE, KIND, DOC)
//  - NAME: symbol EV_*
//  - SRC : EV_SRC_*
//  - CODE: wartosc kodu (uint16_t)
//  - KIND: NONE | COPY | LEASE | STREAM
//  - DOC : krotki opis (debug/CLI)

#define EV_SCHEMA(X) \
    /* SYS */ \
    X(EV_SYS_START,        EV_SRC_SYS,   0x0001, NONE,  "start systemu") \
    \
    /* TIMER */ \
    X(EV_TICK_100MS,       EV_SRC_TIMER, 0x1000, NONE,  "tick 100ms") \
    X(EV_TICK_1S,          EV_SRC_TIMER, 0x1001, NONE,  "tick 1s") \
    \
    /* I2C */ \
    X(EV_I2C_DONE,         EV_SRC_I2C,   0x2000, COPY,  "I2C done: a0=user, a1=0") \
    X(EV_I2C_ERROR,        EV_SRC_I2C,   0x2001, COPY,  "I2C error: a0=user, a1=esp_err_t") \
    \
    /* LCD status */ \
    X(EV_LCD_READY,        EV_SRC_LCD,   0x3001, NONE,  "LCD ready") \
    X(EV_LCD_UPDATED,      EV_SRC_LCD,   0x3002, NONE,  "LCD updated/internal tick") \
    X(EV_LCD_ERROR,        EV_SRC_LCD,   0x30FF, COPY,  "LCD error: a0=code, a1=detail") \
    \
    /* LCD commands (adapter) */ \
    X(EV_LCD_CMD_DRAW_ROW, EV_SRC_LCD,   0x3010, LEASE, "LCD cmd: draw row (lease)") \
    X(EV_LCD_CMD_SET_RGB,  EV_SRC_LCD,   0x3011, COPY,  "LCD cmd: set rgb (a0=packRGB)") \
    X(EV_LCD_CMD_FLUSH,    EV_SRC_LCD,   0x3012, NONE,  "LCD cmd: flush") \
    \
    /* DS18B20 */ \
    X(EV_DS18_READY,       EV_SRC_DS18,  0x4000, COPY,  "DS18 ready (a0=temp_mC or internal tick)") \
    X(EV_DS18_ERROR,       EV_SRC_DS18,  0x4001, COPY,  "DS18 error (a0=err)") \
    \
    /* LOG */ \
    X(EV_LOG_NEW,          EV_SRC_LOG,   0x5000, LEASE, "log line (lease payload)")
