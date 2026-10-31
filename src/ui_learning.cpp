// WatchDog v1 — learning + settings implementation.
#include "ui_learning.h"
#include "ui_common.h"
#include "baseline.h"
#include "ui_known.h"
#include "detectors.h"
#include "sweep.h"

void uiLearningDraw() {
  tft.fillScreen(WD_BG);
  drawMainHeader();

  tft.setTextColor(WD_ORANGE, WD_BG);
  tft.setTextSize(3);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("LEARNING", 160, 60);

  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  tft.drawString(g_additiveLearn ? "Folding in new devices..."
                                 : "Cataloging your airwaves...",
                 160, 88);

  // devices so far
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%d", baselineCount());
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(4);
  tft.drawString(cnt, 160, 125);
  tft.setTextSize(1);
  tft.setTextColor(WD_DIM, WD_BG);
  tft.drawString("devices known", 160, 152);

  if (baselineOverflowCount() > 0) {
    char ov[40];
    snprintf(ov, sizeof(ov), "Baseline FULL: %d not recorded", baselineOverflowCount());
    tft.setTextColor(WD_RED, WD_BG);
    tft.drawString(ov, 160, 100);
  }

  // progress bar
  uint32_t total = g_additiveLearn ? WD_LEARN_ADD_SECS : WD_LEARN_FULL_SECS;
  uint32_t now = wdNow();
  uint32_t elapsed = 0;
  if (g_learnEndsAt > now) {
    uint32_t remain = g_learnEndsAt - now;
    elapsed = (remain >= total) ? 0 : (total - remain);
  } else {
    elapsed = total;
  }
  int bw = 260;
  tft.drawRoundRect(160 - bw / 2, 168, bw, 14, 7, WD_LINE);
  int fill = (int)((uint64_t)elapsed * (bw - 4) / total);
  if (fill > 0) tft.fillRoundRect(160 - bw / 2 + 2, 170, fill, 10, 5, WD_GREEN);

  // time remaining
  char rem[32];
  if (g_learnEndsAt > now) {
    uint32_t r = g_learnEndsAt - now;
    snprintf(rem, sizeof(rem), "%uh %um left", (unsigned)(r / 3600), (unsigned)((r % 3600) / 60));
  } else {
    snprintf(rem, sizeof(rem), "finishing...");
  }
  tft.setTextColor(WD_DIM, WD_BG);
  tft.drawString(rem, 160, 192);
  tft.setTextDatum(TL_DATUM);

  drawButton(16, 204, 140, 30, "Review", WD_TEXT, 2);
  drawButton(164, 204, 140, 30, "Arm now", WD_GREEN, 2);
}

static void armNow() {
  g_mode = WdMode::ARMED;
  g_additiveLearn = false;
  g_screen = WdScreen::DASHBOARD;
  baselineSave();
  g_dirty = true;
}

bool uiLearningTouch(int x, int y) {
  if (inRect(x, y, 16, 204, 140, 30)) {   // review what will be trusted
    uiKnownReset();
    g_screen = WdScreen::KNOWN; g_dirty = true; return true;
  }
  if (inRect(x, y, 164, 204, 140, 30)) {  // ask first
    g_screen = WdScreen::ARMCONFIRM; g_dirty = true; return true;
  }
  return false;
}

// ---- arm confirm (review prompt) -----------------------------------------------

void uiArmConfirmDraw() {
  tft.fillScreen(WD_BG);
  tft.fillRoundRect(20, 50, 280, 140, 10, WD_BG);
  tft.drawRoundRect(20, 50, 280, 140, 10, WD_ORANGE);
  tft.drawRoundRect(21, 51, 278, 138, 9, WD_ORANGE);
  tft.setTextColor(WD_ORANGE, WD_BG);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Arm now?", 160, 72);
  char msg[48];
  snprintf(msg, sizeof(msg), "All %d devices below become", baselineCount());
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(1);
  tft.drawString(msg, 160, 98);
  tft.drawString("NORMAL and will never alert.", 160, 112);
  tft.setTextColor(WD_DIM, WD_BG);
  tft.drawString("A rogue seen while learning is", 160, 130);
  tft.drawString("trusted too. Review first.", 160, 142);
  tft.setTextDatum(TL_DATUM);
  drawButton(34, 152, 118, 30, "Review", WD_TEXT, 2);
  drawButton(168, 152, 118, 30, "Arm", WD_GREEN, 2);
}

bool uiArmConfirmTouch(int x, int y) {
  if (inRect(x, y, 34, 152, 118, 30)) {
    uiKnownReset();
    g_screen = WdScreen::KNOWN; g_dirty = true; return true;
  }
  if (inRect(x, y, 168, 152, 118, 30)) { armNow(); return true; }
  if (!inRect(x, y, 20, 50, 280, 140)) { g_screen = WdScreen::LEARNING; g_dirty = true; }
  return true;
}

// ---- settings ------------------------------------------------------------------

static const char* sensName() {
  switch (g_sens) {
    case WdSensitivity::SENS_LOW:  return "Low";
    case WdSensitivity::SENS_HIGH: return "High";
    default:                   return "Medium";
  }
}

void uiSettingsDraw() {
  tft.fillScreen(WD_BG);
  drawSubHeader("SETTINGS");

  int y = 44;
  // sensitivity row
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(12, y);
  tft.print("Alert sensitivity");
  char s[24];
  snprintf(s, sizeof(s), "< %s >", sensName());
  tft.setTextColor(WD_GREEN, WD_BG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(s, 312, y + 8);
  tft.setTextDatum(TL_DATUM);
  tft.drawLine(8, y + 30, 312, y + 30, WD_LINE);
  y += 40;

  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setCursor(12, y);
  tft.print("Re-learn: full");
  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  tft.setCursor(12, y + 20);
  tft.print("wipe baseline, restart 24h");
  tft.drawLine(8, y + 36, 312, y + 36, WD_LINE);
  y += 46;

  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(12, y);
  tft.print("Re-learn: quick");
  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  tft.setCursor(12, y + 20);
  tft.print("keep baseline, 4h fold-in");
  tft.drawLine(8, y + 36, 312, y + 36, WD_LINE);
  y += 46;

  if (g_mode == WdMode::SWEEP) {
    // Baseline rows are meaningless mid-sweep: say so instead of offering them.
    tft.setTextColor(WD_ORANGE, WD_BG);
    tft.setCursor(12, y);
    tft.print("Sweep active: baseline");
    tft.setCursor(12, y + 14);
    tft.print("changes are disabled.");
  } else if (g_mode == WdMode::ARMED) {
    tft.setTextColor(WD_TEXT, WD_BG);
    tft.setTextSize(2);
    tft.setCursor(12, y);
    tft.print("Sweep mode");
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(1);
    tft.setCursor(12, y + 20);
    tft.print("rental check - nothing saved");
    tft.drawLine(8, y + 36, 312, y + 36, WD_LINE);
  }

  tft.setTextSize(1);
}

static void startLearn(bool additive) {
  g_mode = WdMode::LEARNING;
  g_additiveLearn = additive;
  g_learnEndsAt = wdNow() + (additive ? WD_LEARN_ADD_SECS : WD_LEARN_FULL_SECS);
  g_screen = WdScreen::LEARNING;
  g_dirty = true;
}

bool uiSettingsTouch(int x, int y) {
  if (y < 30 && x < 110) { g_screen = wdHomeScreen(); g_dirty = true; return true; }
  // sensitivity row (y 44..74)
  if (y >= 44 && y < 78) {
    g_sens = (WdSensitivity)(((int)g_sens + 1) % 3);
    g_dirty = true;
    return true;
  }
  if (g_mode == WdMode::SWEEP) return false;  // baseline rows hidden mid-sweep
  // full re-learn row (y 84..120) -> confirm dialog
  if (y >= 84 && y < 124) {
    g_screen = WdScreen::CONFIRM;
    g_dirty = true;
    return true;
  }
  // quick re-learn row (y 130..170)
  if (y >= 130 && y < 172) {
    startLearn(true);
    return true;
  }
  // sweep row (y 176..218), offered only when ARMED
  if (g_mode == WdMode::ARMED && y >= 176 && y < 220) {
    sweepBegin();
    return true;
  }
  return false;
}

// ---- confirm dialog ---------------------------------------------------------------

void uiConfirmDraw() {
  // dim the settings behind it
  uiSettingsDraw();
  tft.fillRect(0, 0, 320, 240, WD_BG);  // simple: full redraw, no translucency
  tft.fillRoundRect(30, 60, 260, 120, 10, WD_BG);
  tft.drawRoundRect(30, 60, 260, 120, 10, WD_RED);
  tft.drawRoundRect(31, 61, 258, 118, 9, WD_RED);

  tft.setTextColor(WD_RED, WD_BG);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Start over?", 160, 84);

  char msg[64];
  snprintf(msg, sizeof(msg), "This clears %d known", baselineCount());
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(1);
  tft.drawString(msg, 160, 106);
  tft.drawString("devices. Continue?", 160, 120);
  tft.setTextDatum(TL_DATUM);

  drawButton(46, 136, 100, 32, "Yes", WD_RED, 2);
  drawButton(174, 136, 100, 32, "No", WD_GREEN, 2);
}

bool uiConfirmTouch(int x, int y) {
  if (inRect(x, y, 46, 136, 100, 32)) {
    baselineBackupForRelearn();   // keep a copy of the old baseline on flash
    baselineClear();
    detectorsResetTwinBaseline();   // forget remembered SSID->AP pairings too
    startLearn(false);
    return true;
  }
  if (inRect(x, y, 174, 136, 100, 32)) {
    g_screen = WdScreen::SETTINGS;
    g_dirty = true;
    return true;
  }
  return true;  // taps outside do nothing; must choose
}
