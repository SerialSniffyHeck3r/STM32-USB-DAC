/* ===== lcd_ui.h — Global LCD partial-update helper for HD44780 (16x2) =====
 * Target: STM32F407 + LCD16X2 driver (LCD16X2.c / LCD16X2_cfg.c)
 * Purpose: keep a shadow DDRAM buffer, mark dirty cells, and flush only changes.
 * Also supports custom characters (CGRAM 0..7) with invalidation helpers.
 */
#ifndef LCD_UI_H
#define LCD_UI_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Compile-time configuration (override by -D...) ====================== */
#ifndef LCDUI_MAX_ROWS
#define LCDUI_MAX_ROWS   2
#endif
#ifndef LCDUI_MAX_COLS
#define LCDUI_MAX_COLS   16
#endif

#ifndef LCDUI_DEFAULT_INDEX
#define LCDUI_DEFAULT_INDEX  0
#endif

/* === API ================================================================= */
void LCDUI_Init(uint8_t index, uint8_t rows, uint8_t cols, uint8_t clear_hw);
void LCDUI_Clear(void);
void LCDUI_InvalidateAll(void);
void LCDUI_InvalidateByChar(uint8_t ch);

void LCDUI_SetImmediate(uint8_t enable);
uint8_t LCDUI_GetImmediate(void);

/* Shadow만 갱신 + dirty 표시 (Flush는 따로) */
void LCDUI_WriteCharAt(uint8_t row, uint8_t col, uint8_t ch);
void LCDUI_WriteStringAt(uint8_t row, uint8_t col, const char *s);
void LCDUI_WriteNumAt(uint8_t row, uint8_t col, int value, int width, char pad);
void LCDUI_WriteFormatAt(uint8_t row, uint8_t col, const char *fmt, ...);

/* Custom chars (CGRAM 0..7) */
void LCDUI_DefineCustom(uint8_t slot, const uint8_t glyph[8]);
static inline void LCDUI_WriteCustomAt(uint8_t row, uint8_t col, uint8_t slot) {
  LCDUI_WriteCharAt(row, col, (slot & 0x07));
}

/* Flush */
void LCDUI_Flush(void);
uint8_t LCDUI_HasDirty(void);

/* Utils */
uint8_t LCDUI_Rows(void);
uint8_t LCDUI_Cols(void);
uint8_t LCDUI_Index(void);

#ifdef __cplusplus
}
#endif
#endif /* LCD_UI_H */
