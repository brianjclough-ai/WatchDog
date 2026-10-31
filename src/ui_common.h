// WatchDog v1 — shared UI helpers: display + touch objects, theme colors,
// tap detection, buttons, headers.
#pragma once
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "wd_state.h"

// CYD touch wiring: dedicated SPI bus (not the display bus).
#define WD_TOUCH_SCK  25
#define WD_TOUCH_MOSI 32
#define WD_TOUCH_MISO 39
#define WD_TOUCH_CS   33
#define WD_TOUCH_IRQ  36
#define WD_TFT_BL     21

// Theme (dark, matches the approved mockups).
#define WD_BG     0x0000
#define WD_PANEL  0x2104
#define WD_GREEN  0x2FD3
#define WD_ORANGE 0xFD20
#define WD_RED    0xF800
#define WD_TEXT   0xFFFF
#define WD_DIM    0x8410
#define WD_LINE   0x4A69

extern TFT_eSPI tft;
extern XPT2046_Touchscreen wdTouch;

void uiCommonInit();          // display + touch + backlight
bool uiTap(int& x, int& y);   // edge-triggered tap in screen coords
uint32_t uiLastTapDownMs();   // millis() when the most recent tap was pressed
bool inRect(int x, int y, int rx, int ry, int rw, int rh);

// Outlined button with centered label.
void drawButton(int x, int y, int w, int h, const char* label,
                uint16_t color, uint8_t textSize = 2);

// Main header: WatchDog | ARMED/LEARNING pill | clock | gear.
void drawMainHeader();
// Sub-screen header: "< Back" + centered title.
void drawSubHeader(const char* title);
