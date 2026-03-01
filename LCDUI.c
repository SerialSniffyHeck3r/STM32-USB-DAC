/* ===== lcd_ui.c — Global LCD partial-update helper (HD44780 16x2) ===== */
#include "LCDUI.h"
#include "LCD16X2.h"
#include <string.h>
#include <stdio.h>

typedef struct {
  uint8_t index;                 /* LCD16X2_Index */
  uint8_t rows, cols;            /* dimensions (1-based API) */
  uint8_t immediate;             /* 1: flush on each write */

  uint8_t shadow[LCDUI_MAX_ROWS][LCDUI_MAX_COLS];  /* desired DDRAM */
  uint8_t dirty[LCDUI_MAX_ROWS][LCDUI_MAX_COLS];   /* 1: needs push */
} LCDUI_State;

static LCDUI_State g_ui;

static inline uint8_t clamp_row(uint8_t r){ if (r<1) r=1; if (r>g_ui.rows) r=g_ui.rows; return r; }
static inline uint8_t clamp_col(uint8_t c){ if (c<1) c=1; if (c>g_ui.cols) c=g_ui.cols; return c; }

void LCDUI_Init(uint8_t index, uint8_t rows, uint8_t cols, uint8_t clear_hw)
{
  if (rows > LCDUI_MAX_ROWS) rows = LCDUI_MAX_ROWS;
  if (cols > LCDUI_MAX_COLS) cols = LCDUI_MAX_COLS;

  memset(&g_ui, 0, sizeof(g_ui));
  g_ui.index = index;
  g_ui.rows  = rows ? rows : LCDUI_MAX_ROWS;
  g_ui.cols  = cols ? cols : LCDUI_MAX_COLS;
  g_ui.immediate = 0;

  LCD16X2_Init(g_ui.index);
  if (clear_hw) LCD16X2_Clear(g_ui.index);

  for (uint8_t r=0;r<g_ui.rows;r++)
    for (uint8_t c=0;c<g_ui.cols;c++)
      g_ui.shadow[r][c] = ' ';
  LCDUI_InvalidateAll();
}

void LCDUI_Clear(void)
{
  LCD16X2_Clear(g_ui.index);
  for (uint8_t r=0;r<g_ui.rows;r++)
    for (uint8_t c=0;c<g_ui.cols;c++){
      g_ui.shadow[r][c] = ' ';
      g_ui.dirty[r][c]  = 0;
    }
}

void LCDUI_InvalidateAll(void)
{
  for (uint8_t r=0;r<g_ui.rows;r++)
    for (uint8_t c=0;c<g_ui.cols;c++)
      g_ui.dirty[r][c] = 1;
}

void LCDUI_InvalidateByChar(uint8_t ch)
{
  for (uint8_t r=0;r<g_ui.rows;r++)
    for (uint8_t c=0;c<g_ui.cols;c++)
      if (g_ui.shadow[r][c] == ch) g_ui.dirty[r][c] = 1;
}

void LCDUI_SetImmediate(uint8_t enable){ g_ui.immediate = enable ? 1 : 0; }
uint8_t LCDUI_GetImmediate(void){ return g_ui.immediate; }

uint8_t LCDUI_Rows(void){ return g_ui.rows; }
uint8_t LCDUI_Cols(void){ return g_ui.cols; }
uint8_t LCDUI_Index(void){ return g_ui.index; }

/* --- Core shadow update -------------------------------------------------- */
static inline void put_char(uint8_t row, uint8_t col, uint8_t ch)
{
  row = clamp_row(row); col = clamp_col(col);
  uint8_t r = row-1, c = col-1;
  if (g_ui.shadow[r][c] != ch){
    g_ui.shadow[r][c] = ch;
    g_ui.dirty[r][c]  = 1;
  }
}

void LCDUI_WriteCharAt(uint8_t row, uint8_t col, uint8_t ch)
{
  put_char(row, col, ch);
  if (g_ui.immediate) LCDUI_Flush();
}

void LCDUI_WriteStringAt(uint8_t row, uint8_t col, const char *s)
{
  if (!s) return;
  uint8_t r = clamp_row(row);
  uint8_t c = clamp_col(col);
  uint8_t maxc = g_ui.cols;
  for (uint8_t i=0; s[i] && c<=maxc; i++, c++){
    put_char(r, c, (uint8_t)s[i]);
  }
  if (g_ui.immediate) LCDUI_Flush();
}

void LCDUI_WriteNumAt(uint8_t row, uint8_t col, int value, int width, char pad)
{
  char buf[24];
  if (width < 1) width = 1;
  if (width > (int)sizeof(buf)-1) width = sizeof(buf)-1;

  char num[24]; snprintf(num, sizeof(num), "%d", value);
  int len = (int)strlen(num);
  if (len >= width) {
    LCDUI_WriteStringAt(row, col, num + (len - width));
  } else {
    int padlen = width - len;
    for (int i=0;i<padlen;i++) buf[i] = pad;
    memcpy(buf+padlen, num, len);
    buf[width] = 0;
    LCDUI_WriteStringAt(row, col, buf);
  }
}

void LCDUI_WriteFormatAt(uint8_t row, uint8_t col, const char *fmt, ...)
{
  char buf[32];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  LCDUI_WriteStringAt(row, col, buf);
}

/* --- Custom chars -------------------------------------------------------- */
void LCDUI_DefineCustom(uint8_t slot, const uint8_t glyph[8])
{
  if (!glyph) return;
  slot &= 0x07;
  /* Reprogram CGRAM; invalidate same-code cells for safety. */
  LCD16X2_RegisterCustomChar(g_ui.index, slot, (uint8_t*)glyph);
  LCDUI_InvalidateByChar(slot);
}

/* --- Flusher: push only dirty cells ------------------------------------- */
void LCDUI_Flush(void)
{
  for (uint8_t r=0; r<g_ui.rows; r++){
    uint8_t c = 0;
    while (c < g_ui.cols){
      while (c < g_ui.cols && !g_ui.dirty[r][c]) c++;
      if (c >= g_ui.cols) break;

      uint8_t start_c = c;
      while (c < g_ui.cols && g_ui.dirty[r][c]) c++;
      uint8_t end_c = c; /* [start_c, end_c) */

      LCD16X2_Set_Cursor(g_ui.index, (uint8_t)(r+1), (uint8_t)(start_c+1));
      for (uint8_t x = start_c; x < end_c; x++){
        LCD16X2_Write_Char(g_ui.index, (char)g_ui.shadow[r][x]); /* works with 0..7 custom */
        g_ui.dirty[r][x] = 0;
      }
    }
  }
}

uint8_t LCDUI_HasDirty(void)
{
  for (uint8_t r=0;r<g_ui.rows;r++)
    for (uint8_t c=0;c<g_ui.cols;c++)
      if (g_ui.dirty[r][c]) return 1;
  return 0;
}
