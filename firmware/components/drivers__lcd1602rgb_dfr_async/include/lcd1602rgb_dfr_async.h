/**
 * @file lcd1602rgb_dfr_async.h
 * @brief Sterownik DFRobot LCD1602 RGB (DFR0464 v2.0) – w pełni event‑driven.
 *
 * Obsługiwane adresy:
 *  - Kontroler LCD: 0x3E (ST7032-kompatybilny)
 *  - Kontroler RGB: 0x2D (PCA9633-compat)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ports/i2c_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_dev_t* dev_lcd;   ///< urządzenie 0x3E
    i2c_dev_t* dev_rgb;   ///< urządzenie 0x2D
} lcd1602rgb_cfg_t;

/** Inicjalizacja – rejestruje się w ev-busie i startuje automat po EV_SYS_START. */
bool lcd1602rgb_init(const lcd1602rgb_cfg_t* cfg);

/** Ustawienie koloru podświetlenia (0..255). */
void lcd1602rgb_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/** Napis w danym wierszu i kolumnie (0-based). Tekst zostaje w lokalnym buforze. */
void lcd1602rgb_draw_text(uint8_t col, uint8_t row, const char* utf8);

/** Żądanie wypchnięcia zmian do LCD (flush). Nieblokujące. */
void lcd1602rgb_request_flush(void);

#ifdef __cplusplus
}
#endif
