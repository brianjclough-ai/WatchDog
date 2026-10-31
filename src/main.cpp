// WatchDog v1 — main: setup/loop, top-level mode state machine,
// scan scheduler, touch dispatch, screen dispatch.
#include <Arduino.h>
#include "wd_state.h"
#include "ui_common.h"
#include "ui_dashboard.h"
#include "ui_alert.h"
#include "ui_detail.h"
#include "ui_known.h"
#include "ui_learning.h"
#include "ui_history.h"
#include "sweep.h"
#include "state_store.h"
#include "scan.h"
#include "baseline.h"
#include "detectors.h"

// ---- globals (declared in wd_state.h) ----
WdMode        g_mode = WdMode::LEARNING;
WdScreen      g_screen = WdScreen::LEARNING;
WdTile        g_detailTile = WdTile::WIFI;
WdTiles       g_tiles;
WdSensitivity g_sens = WdSensitivity::SENS_MEDIUM;
bool          g_alertPopup = false;
WdAlert       g_popup;
bool          g_additiveLearn = false;
uint32_t      g_learnEndsAt = 0;
uint32_t      g_bootMillis = 0;
uint32_t      g_popupShownMs = 0;
uint32_t      g_lastCycleAt = 0;
bool          g_dirty = true;
WdKbPurpose   g_kbPurpose = WdKbPurpose::RENAME_DEVICE;

void wdAgeStr(uint32_t thenSec, char* buf, size_t bufSz) {
  if (thenSec == WD_AT_PREV_BOOT) { strlcpy(buf, "prev boot", bufSz); return; }
  uint32_t now = wdNow();
  uint32_t d = (now >= thenSec) ? (now - thenSec) : 0;
  if (d < 60) snprintf(buf, bufSz, "%us", (unsigned)d);
  else if (d < 3600) snprintf(buf, bufSz, "%um", (unsigned)(d / 60));
  else snprintf(buf, bufSz, "%uh", (unsigned)(d / 3600));
}

void wdClockStr(char* buf, size_t bufSz) {
  uint32_t s = wdNow();
  snprintf(buf, bufSz, "%02u:%02u", (unsigned)((s / 3600) % 24),
           (unsigned)((s / 60) % 60));
}

static void drawScreen() {
  switch (g_screen) {
    case WdScreen::DASHBOARD: uiDashboardDraw(); break;
    case WdScreen::DETAIL:    uiDetailDraw(); break;
    case WdScreen::KNOWN:     uiKnownDraw(); break;
    case WdScreen::LEARNING:  uiLearningDraw(); break;
    case WdScreen::SETTINGS:  uiSettingsDraw(); break;
    case WdScreen::CONFIRM:   uiConfirmDraw(); break;
    case WdScreen::KEYBOARD:  uiKeyboardDraw(); break;
    case WdScreen::HISTORY:   uiHistoryDraw(); break;
    case WdScreen::ARMCONFIRM: uiArmConfirmDraw(); break;
    case WdScreen::SWEEP_ROOMS:   uiSweepRoomsDraw(); break;
    case WdScreen::SWEEP_SUMMARY:  uiSweepSummaryDraw(); break;
  }
  if (g_alertPopup) uiAlertDraw();
}

static void dispatchTouch(int x, int y) {
  if (g_alertPopup) {
    // A press that began BEFORE the popup appeared must not hit its buttons.
    if (uiLastTapDownMs() < g_popupShownMs) return;
    uiAlertTouch(x, y);
    return;
  }
  bool consumed = false;
  switch (g_screen) {
    case WdScreen::DASHBOARD: consumed = uiDashboardTouch(x, y); break;
    case WdScreen::DETAIL:    consumed = uiDetailTouch(x, y); break;
    case WdScreen::KNOWN:     consumed = uiKnownTouch(x, y); break;
    case WdScreen::LEARNING:  consumed = uiLearningTouch(x, y); break;
    case WdScreen::SETTINGS:  consumed = uiSettingsTouch(x, y); break;
    case WdScreen::CONFIRM:   consumed = uiConfirmTouch(x, y); break;
    case WdScreen::KEYBOARD:  consumed = uiKeyboardTouch(x, y); break;
    case WdScreen::HISTORY:   consumed = uiHistoryTouch(x, y); break;
    case WdScreen::ARMCONFIRM: consumed = uiArmConfirmTouch(x, y); break;
    case WdScreen::SWEEP_ROOMS:   consumed = uiSweepRoomsTouch(x, y); break;
    case WdScreen::SWEEP_SUMMARY:  consumed = uiSweepSummaryTouch(x, y); break;
  }
  (void)consumed;
}

void setup() {
  Serial.begin(115200);
  g_bootMillis = millis();

  uiCommonInit();
  baselineInit();
  detectorsInit();
  scanInit();

  // Restore sensitivity + learn progress (uptime resets on power loss, so the
  // REMAINING learn time is what is stored).
  WdSaved sv = wdStateLoad();
  if (sv.valid) g_sens = (WdSensitivity)sv.sens;

  if (sv.valid && sv.learning && sv.remain > 0) {
    // Power was lost mid-learning: resume, do not arm on a half-built baseline.
    g_mode = WdMode::LEARNING;
    g_additiveLearn = sv.additive;
    g_learnEndsAt = wdNow() + sv.remain;
    g_screen = WdScreen::LEARNING;
  } else if (baselineCount() == 0) {
    // First boot (or baseline lost): full learning.
    g_mode = WdMode::LEARNING;
    g_additiveLearn = false;
    g_learnEndsAt = wdNow() + WD_LEARN_FULL_SECS;
    g_screen = WdScreen::LEARNING;
  } else {
    g_mode = WdMode::ARMED;
    g_screen = WdScreen::DASHBOARD;
  }
  g_tiles.known = baselineCount();
  g_lastCycleAt = wdNow();
  g_dirty = true;
  drawScreen();
  g_dirty = false;
}

void loop() {
  scanTick();
  baselineTick();
  detectorsTick();   // persists twin baseline + alert history when they change

  // Learning window expired -> arm automatically.
  if (g_mode == WdMode::LEARNING && wdNow() >= g_learnEndsAt) {
    g_mode = WdMode::ARMED;
    g_additiveLearn = false;
    g_screen = WdScreen::DASHBOARD;
    baselineSave();
    g_dirty = true;
  }

  // Persist mode / sensitivity changes immediately, learn progress every 5 min.
  {
    static bool first = true;
    static WdMode lastMode = WdMode::LEARNING;
    static bool lastAdd = false;
    static WdSensitivity lastSens = WdSensitivity::SENS_MEDIUM;
    static uint32_t lastSaveMs = 0;
    bool changed = first || g_mode != lastMode || g_additiveLearn != lastAdd || g_sens != lastSens;
    if (changed || (g_mode == WdMode::LEARNING && millis() - lastSaveMs > 300000UL)) {
      uint32_t now = wdNow();
      uint32_t remain = (g_mode == WdMode::LEARNING && g_learnEndsAt > now) ? (g_learnEndsAt - now) : 0;
      wdStateSave(g_mode == WdMode::LEARNING, g_additiveLearn, remain, (uint8_t)g_sens);
      first = false; lastMode = g_mode; lastAdd = g_additiveLearn; lastSens = g_sens;
      lastSaveMs = millis();
    }
  }

  int tx = 0, ty = 0;
  if (uiTap(tx, ty)) dispatchTouch(tx, ty);

  // Redraw on state changes; plus a slow tick for the clock/progress.
  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if (g_dirty || now - lastDraw > 2000) {
    drawScreen();
    g_dirty = false;
    lastDraw = now;
  }
}
