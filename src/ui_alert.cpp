// WatchDog v1 — alert popup implementation.
#include "ui_alert.h"
#include "ui_common.h"
#include "baseline.h"
#include "detectors.h"
#include <string.h>

#define POP_X 20
#define POP_Y 45
#define POP_W 280
#define POP_H 150

static void popupButton(int idx, int count, const char* label, uint16_t color,
                        const char* sub = nullptr) {
  int bw = (POP_W - 16 - (count - 1) * 8) / count;
  int bx = POP_X + 8 + idx * (bw + 8);
  int by = POP_Y + POP_H - 44;
  drawButton(bx, by, bw, 36, label, color, 2);
  if (sub) {
    tft.setTextColor(color, WD_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(sub, bx + bw / 2, by + 28);
    tft.setTextDatum(TL_DATUM);
  }
}

void uiAlertDraw() {
  uint16_t border = (g_popup.level == WdAlertLevel::WARNING) ? WD_ORANGE : WD_GREEN;
  tft.fillRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, WD_BG);
  tft.drawRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, border);
  tft.drawRoundRect(POP_X + 1, POP_Y + 1, POP_W - 2, POP_H - 2, 9, border);

  tft.setTextColor(border, WD_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  const bool isDevice = (g_popup.tile == WdTile::WIFI || g_popup.tile == WdTile::BLE);
  tft.drawString(isDevice ? "NEW DEVICE" : "DETECTION - VERIFY", POP_X + POP_W / 2, POP_Y + 14);

  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.drawString(g_popup.title, POP_X + POP_W / 2, POP_Y + 38);

  tft.setTextSize(1);
  tft.setTextColor(WD_DIM, WD_BG);
  tft.drawString(g_popup.detail, POP_X + POP_W / 2, POP_Y + 60);
  tft.drawString(g_popup.reason, POP_X + POP_W / 2, POP_Y + 74);

  char meta[48];
  char age[16];
  wdAgeStr(g_popup.at, age, sizeof(age));
  snprintf(meta, sizeof(meta), "RSSI %d  -  %s ago", (int)g_popup.rssi, age);
  tft.drawString(meta, POP_X + POP_W / 2, POP_Y + 88);
  tft.setTextDatum(TL_DATUM);

  bool hasDevice = (g_popup.radio != 255 && g_popup.mac[0] != 0);
  bool sweep = (g_mode == WdMode::SWEEP);
  if (hasDevice) {
    // In sweep mode Trust lands in the RAM session table only: say so.
    popupButton(0, 3, "Trust", WD_GREEN, sweep ? "(sweep only)" : nullptr);
    popupButton(1, 3, "Watch", WD_ORANGE);
    popupButton(2, 3, "Details", WD_TEXT);
  } else {
    popupButton(0, 2, "Watch", WD_ORANGE);
    popupButton(1, 2, "Details", WD_TEXT);
  }
}

static bool popupHit(int idx, int count, int x, int y) {
  int bw = (POP_W - 16 - (count - 1) * 8) / count;
  int bx = POP_X + 8 + idx * (bw + 8);
  int by = POP_Y + POP_H - 44;
  return inRect(x, y, bx, by, bw, 36);
}

bool uiAlertTouch(int x, int y) {
  bool hasDevice = (g_popup.radio != 255 && g_popup.mac[0] != 0);
  int count = hasDevice ? 3 : 2;

  // Taps outside the popup dismiss it (but don't act).
  if (!inRect(x, y, POP_X, POP_Y, POP_W, POP_H)) {
    g_alertPopup = false;
    detectorsAlertDismissed();   // dismissed alerts stay in the history
    g_dirty = true;
    return true;
  }

  int action = -1;  // 0 = Trust, 1 = Watch, 2 = Details
  for (int i = 0; i < count; i++)
    if (popupHit(i, count, x, y)) action = hasDevice ? i : i + 1;

  if (action == 0 && hasDevice) {
    // Trust: baseline the device so it never alerts again.
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "Trusted %.12s", g_popup.mac + 6);
    if (baselineAdd(g_popup.mac, g_popup.radio, g_popup.fpHash, g_popup.rssi, lbl) < 0) {
      // List is full: say so instead of pretending it was trusted.
      strlcpy(g_popup.reason, "Baseline full - NOT trusted", sizeof(g_popup.reason));
      g_dirty = true;
      return true;   // keep the popup open
    }
  } else if (action == 1) {
    if (hasDevice) detectorsSnooze(g_popup.mac);
  } else if (action == 2) {
    g_detailTile = g_popup.tile;
    g_screen = WdScreen::DETAIL;
  }
  g_alertPopup = false;
  detectorsAlertDismissed();   // show the next queued alert, if any
  g_dirty = true;
  return true;
}
