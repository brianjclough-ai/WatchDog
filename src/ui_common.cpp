// WatchDog v1 — shared UI helpers implementation.
#include "ui_common.h"
#include <SPI.h>

TFT_eSPI tft;
XPT2046_Touchscreen wdTouch(WD_TOUCH_CS, WD_TOUCH_IRQ);
static SPIClass s_touchSPI(HSPI);

// XPT2046_Touchscreen::getPoint() returns RAW ADC values (0..4095), not
// pixels. These bounds map them to the 320x240 landscape screen (typical
// CYD values). If taps land offset, tune the four numbers; if they land
// mirrored, build with -DWD_TOUCH_FLIP_X and/or -DWD_TOUCH_FLIP_Y.
#ifndef WD_TOUCH_X_MIN
#define WD_TOUCH_X_MIN 200
#endif
#ifndef WD_TOUCH_X_MAX
#define WD_TOUCH_X_MAX 3700
#endif
#ifndef WD_TOUCH_Y_MIN
#define WD_TOUCH_Y_MIN 240
#endif
#ifndef WD_TOUCH_Y_MAX
#define WD_TOUCH_Y_MAX 3800
#endif

static uint32_t s_lastDownMs = 0;
uint32_t uiLastTapDownMs() { return s_lastDownMs; }

void uiCommonInit() {
  pinMode(WD_TFT_BL, OUTPUT);
  digitalWrite(WD_TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);  // 320x240 landscape
  tft.fillScreen(WD_BG);
  s_touchSPI.begin(WD_TOUCH_SCK, WD_TOUCH_MISO, WD_TOUCH_MOSI, WD_TOUCH_CS);
  wdTouch.begin(s_touchSPI);
  wdTouch.setRotation(1);
}

bool uiTap(int& x, int& y) {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static int dx = 0, dy = 0;
  bool down = wdTouch.tirqTouched() && wdTouch.touched();
  uint32_t now = millis();
  if (down && !wasDown) {
    TS_Point p = wdTouch.getPoint();
    dx = (int)map((long)p.x, WD_TOUCH_X_MIN, WD_TOUCH_X_MAX, 0, 319);
    dy = (int)map((long)p.y, WD_TOUCH_Y_MIN, WD_TOUCH_Y_MAX, 0, 239);
    if (dx < 0) dx = 0; else if (dx >= 320) dx = 319;
    if (dy < 0) dy = 0; else if (dy >= 240) dy = 239;
#ifdef WD_TOUCH_FLIP_X
    dx = 319 - dx;
#endif
#ifdef WD_TOUCH_FLIP_Y
    dy = 239 - dy;
#endif
    downAt = now;
    s_lastDownMs = now;
    wasDown = true;
    return false;
  }
  if (!down && wasDown) {
    wasDown = false;
    if (now - downAt > 3000) return false;  // it was a hold, not a tap
    x = dx; y = dy;
    return true;
  }
  return false;
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void drawButton(int x, int y, int w, int h, const char* label,
                uint16_t color, uint8_t textSize) {
  tft.drawRoundRect(x, y, w, h, 6, color);
  tft.setTextColor(color, WD_BG);
  tft.setTextSize(textSize);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w / 2, y + h / 2 + 1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
}

void drawMainHeader() {
  tft.fillRect(0, 0, 320, 30, WD_BG);
  tft.drawLine(0, 30, 320, 30, WD_LINE);
  // shield dot + name
  tft.fillCircle(16, 15, 8, WD_GREEN);
  tft.setTextColor(WD_GREEN, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(30, 8);
  tft.print("WatchDog");
  // mode pill
  const char* mode;
  uint16_t mc;
  int pw;
  if (g_mode == WdMode::ARMED)      { mode = "ARMED";    mc = WD_GREEN;  pw = 64; }
  else if (g_mode == WdMode::SWEEP) { mode = "SWEEP";    mc = WD_ORANGE; pw = 64; }
  else                              { mode = "LEARNING"; mc = WD_ORANGE; pw = 88; }
  int px = 320 - pw - 66;
  tft.fillRoundRect(px, 5, pw, 20, 10, mc);
  tft.setTextColor(WD_BG, mc);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(mode, px + pw / 2, 16);
  tft.setTextDatum(TL_DATUM);
  // clock (uptime HH:MM)
  char clk[8];
  wdClockStr(clk, sizeof(clk));
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(320 - 60, 8);
  tft.print(clk);
  tft.setTextSize(1);
}

void drawSubHeader(const char* title) {
  tft.fillRect(0, 0, 320, 30, WD_BG);
  tft.drawLine(0, 30, 320, 30, WD_LINE);
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("< Back");
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, 170, 16);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
}
