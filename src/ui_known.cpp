// WatchDog v1 — known devices + rename keyboard implementation.
#include "ui_known.h"
#include "ui_common.h"
#include "baseline.h"
#include "sweep.h"

#define ROWS_PER_PAGE 4
#define ROW_H 40
#define LIST_Y 36

static int s_page = 0;
static int s_sel = -1;      // selected baseline index
static int s_editIdx = -1;  // index being renamed
static char s_buf[32];
static uint32_t s_removeArmedMs = 0;   // two-tap Remove: first tap arms, second confirms

void uiKnownReset() { s_page = 0; s_sel = -1; }

// Shared text entry: rename a device, or name a new sweep room.
void uiKeyboardStart(WdKbPurpose purpose, const char* initial, int editIdx) {
  g_kbPurpose = purpose;
  s_editIdx = editIdx;
  strlcpy(s_buf, initial ? initial : "", sizeof(s_buf));
  g_screen = WdScreen::KEYBOARD;
  g_dirty = true;
}

static void drawCheck(int x, int y) {
  tft.drawCircle(x, y, 9, WD_GREEN);
  tft.drawLine(x - 5, y, x - 1, y + 4, WD_GREEN);
  tft.drawLine(x - 1, y + 4, x + 6, y - 4, WD_GREEN);
}

void uiKnownDraw() {
  tft.fillScreen(WD_BG);
  int n = baselineCount();
  char title[40];
  snprintf(title, sizeof(title), "KNOWN: %d", n);
  drawSubHeader(title);

  int pages = (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
  if (pages < 1) pages = 1;
  if (s_page >= pages) s_page = pages - 1;
  if (s_sel >= n) s_sel = -1;

  if (n == 0) {
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No known devices yet", 160, 120);
    tft.setTextDatum(TL_DATUM);
  } else {
    for (int r = 0; r < ROWS_PER_PAGE; r++) {
      int i = s_page * ROWS_PER_PAGE + r;
      if (i >= n) break;
      int y = LIST_Y + r * ROW_H;
      WdDevice* d = baselineAt(i);
      if (!d) break;
      if (i == s_sel) tft.fillRoundRect(4, y - 4, 312, ROW_H, 6, WD_PANEL);
      drawCheck(22, y + 12);
      tft.setTextColor(WD_TEXT, i == s_sel ? WD_PANEL : WD_BG);
      tft.setTextSize(2);
      tft.setCursor(40, y);
      char nm[24];
      strncpy(nm, d->label, 22); nm[22] = 0;
      tft.print(nm);
      tft.setTextColor(WD_DIM, i == s_sel ? WD_PANEL : WD_BG);
      tft.setTextSize(1);
      tft.setCursor(40, y + 22);
      tft.print(d->radio == 0 ? "WiFi " : "BLE ");
      tft.print(d->mac);
      tft.setTextSize(1);
    }
  }

  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextDatum(MC_DATUM);
  if (pages > 1) {
    char pg[16];
    snprintf(pg, sizeof(pg), "%d / %d", s_page + 1, pages);
    tft.drawString(pg, 160, 222);
  }
  tft.setTextDatum(TL_DATUM);
  uint16_t rc = (s_sel >= 0) ? WD_GREEN : WD_DIM;
  uint16_t mc = (s_sel >= 0) ? WD_RED : WD_DIM;
  drawButton(8, 204, 150, 30, "Rename", rc, 2);
  bool armed = (s_sel >= 0) && (millis() - s_removeArmedMs < 4000);
  drawButton(162, 204, 150, 30, armed ? "Sure?" : "Remove", armed ? WD_RED : mc, 2);
}

bool uiKnownTouch(int x, int y) {
  if (y < 30 && x < 110) { g_screen = wdHomeScreen(); g_dirty = true; return true; }
  int n = baselineCount();
  // row select
  for (int r = 0; r < ROWS_PER_PAGE; r++) {
    int i = s_page * ROWS_PER_PAGE + r;
    if (i >= n) break;
    int ry = LIST_Y + r * ROW_H;
    if (inRect(x, y, 4, ry - 4, 312, ROW_H)) {
      s_sel = (s_sel == i) ? -1 : i;
      s_removeArmedMs = 0;
      g_dirty = true;
      return true;
    }
  }
  if (s_sel < 0) return false;
  if (inRect(x, y, 8, 204, 150, 30)) {
    // Rename -> keyboard
    WdDevice* d = baselineAt(s_sel);
    if (d) uiKeyboardStart(WdKbPurpose::RENAME_DEVICE, d->label, s_sel);
    return true;
  }
  if (inRect(x, y, 162, 204, 150, 30)) {
    if (millis() - s_removeArmedMs < 4000) {   // second tap within 4 s: really remove
      baselineRemove(s_sel);
      s_sel = -1;
      s_removeArmedMs = 0;
    } else {
      s_removeArmedMs = millis();              // first tap: arm, show "Sure?"
    }
    g_dirty = true;
    return true;
  }
  return false;
}

// ---- rename keyboard ---------------------------------------------------------

static const char* kRows[] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL-",
  "ZXCVBNM ",
};
#define KEY_W 30
#define KEY_H 34
#define KEY_X0 10
#define KEY_Y0 64

static void keyAt(int idx, int row, int& x, int& y) {
  int n = strlen(kRows[row]);
  int totalW = n * (KEY_W + 2);
  x = (320 - totalW) / 2 + idx * (KEY_W + 2);
  y = KEY_Y0 + row * (KEY_H + 4);
}

void uiKeyboardDraw() {
  tft.fillScreen(WD_BG);
  drawSubHeader(g_kbPurpose == WdKbPurpose::ADD_ROOM ? "ADD ROOM" : "RENAME");
  // buffer
  tft.drawRoundRect(10, 34, 300, 24, 4, WD_LINE);
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 40);
  tft.print(s_buf);
  // keys
  tft.setTextSize(2);
  for (int r = 0; r < 4; r++) {
    int n = strlen(kRows[r]);
    for (int i = 0; i < n; i++) {
      int x, y;
      keyAt(i, r, x, y);
      char lbl[2] = {kRows[r][i], 0};
      if (lbl[0] == ' ') continue;  // placeholder
      tft.drawRoundRect(x, y, KEY_W, KEY_H, 4, WD_LINE);
      tft.setTextColor(WD_TEXT, WD_BG);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(lbl, x + KEY_W / 2, y + KEY_H / 2 + 1);
      tft.setTextDatum(TL_DATUM);
    }
  }
  // bottom row: backspace, space, OK
  drawButton(10, 208, 90, 28, "<-", WD_ORANGE, 2);
  drawButton(108, 208, 104, 28, "space", WD_DIM, 1);
  drawButton(220, 208, 90, 28, "OK", WD_GREEN, 2);
  tft.setTextSize(1);
}

bool uiKeyboardTouch(int x, int y) {
  WdScreen backTo = (g_kbPurpose == WdKbPurpose::ADD_ROOM)
                      ? WdScreen::SWEEP_ROOMS : WdScreen::KNOWN;
  if (y < 30 && x < 110) {
    g_kbPurpose = WdKbPurpose::RENAME_DEVICE;
    g_screen = backTo; g_dirty = true; return true;
  }
  size_t len = strlen(s_buf);
  for (int r = 0; r < 4; r++) {
    int n = strlen(kRows[r]);
    for (int i = 0; i < n; i++) {
      if (kRows[r][i] == ' ') continue;
      int kx, ky;
      keyAt(i, r, kx, ky);
      if (inRect(x, y, kx, ky, KEY_W, KEY_H)) {
        if (len < sizeof(s_buf) - 1) {
          s_buf[len] = kRows[r][i];
          s_buf[len + 1] = 0;
        }
        g_dirty = true;
        return true;
      }
    }
  }
  if (inRect(x, y, 10, 208, 90, 28)) {  // backspace
    if (len > 0) s_buf[len - 1] = 0;
    g_dirty = true;
    return true;
  }
  if (inRect(x, y, 108, 208, 104, 28)) {  // space
    if (len < sizeof(s_buf) - 1) { s_buf[len] = ' '; s_buf[len + 1] = 0; }
    g_dirty = true;
    return true;
  }
  if (inRect(x, y, 220, 208, 90, 28)) {  // OK
    if (g_kbPurpose == WdKbPurpose::ADD_ROOM) {
      if (s_buf[0]) sweepAddRoom(s_buf);
      g_screen = WdScreen::SWEEP_ROOMS;
    } else {
      if (s_editIdx >= 0 && s_buf[0]) baselineRename(s_editIdx, s_buf);
      g_screen = WdScreen::KNOWN;
    }
    g_kbPurpose = WdKbPurpose::RENAME_DEVICE;
    g_dirty = true;
    return true;
  }
  return false;
}
