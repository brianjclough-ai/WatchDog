// WatchDog v1.1.0 — sweep mode implementation.
#include "sweep.h"
#include "ui_common.h"
#include "ui_known.h"
#include "baseline.h"
#include "detectors.h"
#include <string.h>

#define SWEEP_ROOMS_MAX 8
#define ROOM_ROWS_VISIBLE 6
#define ROW_H 24
#define LIST_Y0 50

struct SweepRoom {
  char     name[24];
  bool     done;
  uint32_t alertsAtSel;  // detectorsAlertTotal() when this room became current
  uint32_t findings;     // finalized findings from earlier visits
};

static SweepRoom s_rooms[SWEEP_ROOMS_MAX];
static int       s_roomN = 0;
static int       s_cur = -1;     // room currently being swept
static int       s_top = 0;      // scroll offset

static void addRoom(const char* name) {
  if (s_roomN >= SWEEP_ROOMS_MAX || !name || !name[0]) return;
  strlcpy(s_rooms[s_roomN].name, name, sizeof(s_rooms[s_roomN].name));
  s_rooms[s_roomN].done = false;
  s_rooms[s_roomN].alertsAtSel = 0;
  s_rooms[s_roomN].findings = 0;
  s_roomN++;
  if (s_roomN > ROOM_ROWS_VISIBLE) s_top = s_roomN - ROOM_ROWS_VISIBLE;
}

void sweepAddRoom(const char* name) { addRoom(name); }

static void selectRoom(int i) {
  if (i < 0 || i >= s_roomN) return;
  if (s_cur >= 0 && s_cur != i) {
    // Finalize the room we're leaving.
    s_rooms[s_cur].findings += detectorsAlertTotal() - s_rooms[s_cur].alertsAtSel;
  }
  s_cur = i;
  s_rooms[i].alertsAtSel = detectorsAlertTotal();
}

static uint32_t roomFindings(int i) {
  uint32_t f = s_rooms[i].findings;
  if (i == s_cur) f += detectorsAlertTotal() - s_rooms[i].alertsAtSel;
  return f;
}

void sweepBegin() {
  baselineSessionBegin();   // RAM-only table from here on
  detectorsSweepBegin();    // park home twin pairings, clear tables + counters
  s_roomN = 0; s_cur = -1; s_top = 0;
  addRoom("Living room");
  addRoom("Kitchen");
  addRoom("Bedroom 1");
  addRoom("Bedroom 2");
  addRoom("Bathroom");
  g_mode = WdMode::SWEEP;
  g_screen = WdScreen::SWEEP_ROOMS;
  g_dirty = true;
}

void sweepEnd() {
  baselineSessionEnd();   // discard the session; home table was never touched
  detectorsSweepEnd();    // restore home twin pairings, drop sweep findings
  g_mode = WdMode::ARMED;
  g_screen = WdScreen::DASHBOARD;
  g_tiles.known = baselineCount();
  g_dirty = true;
}

// ---- room checklist ----------------------------------------------------------

void uiSweepRoomsDraw() {
  tft.fillScreen(WD_BG);
  drawSubHeader("SWEEP");

  tft.setTextColor(WD_ORANGE, WD_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Session only - nothing is saved", 160, 38);
  tft.setTextDatum(TL_DATUM);

  int vis = s_roomN - s_top;
  if (vis > ROOM_ROWS_VISIBLE) vis = ROOM_ROWS_VISIBLE;
  for (int r = 0; r < vis; r++) {
    int i = s_top + r;
    int y = LIST_Y0 + r * ROW_H;
    bool cur = (i == s_cur);
    if (cur) tft.drawRoundRect(6, y - 3, 308, ROW_H, 6, WD_ORANGE);
    // checkbox
    tft.drawRect(12, y + 3, 16, 16, s_rooms[i].done ? WD_GREEN : WD_DIM);
    if (s_rooms[i].done) {
      tft.drawLine(14, y + 11, 19, y + 16, WD_GREEN);
      tft.drawLine(19, y + 16, 27, y + 6, WD_GREEN);
    }
    // name
    tft.setTextColor(s_rooms[i].done ? WD_DIM : WD_TEXT, WD_BG);
    tft.setTextSize(2);
    tft.setCursor(36, y + 3);
    tft.print(s_rooms[i].name);
    // findings
    char f[24];
    snprintf(f, sizeof(f), "%u found", (unsigned)roomFindings(i));
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(f, 308, y + 11);
    tft.setTextDatum(TL_DATUM);
  }
  // scroll hints
  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  if (s_top > 0) { tft.setTextDatum(MC_DATUM); tft.drawString("^", 160, LIST_Y0 - 10); tft.setTextDatum(TL_DATUM); }

  drawButton(12, 206, 140, 28, "Add room", WD_TEXT, 1);
  drawButton(168, 206, 140, 28, "End sweep", WD_ORANGE, 1);
  tft.setTextSize(1);
}

bool uiSweepRoomsTouch(int x, int y) {
  if (y < 30 && x < 110) { g_screen = wdHomeScreen(); g_dirty = true; return true; }
  int vis = s_roomN - s_top;
  if (vis > ROOM_ROWS_VISIBLE) vis = ROOM_ROWS_VISIBLE;
  for (int r = 0; r < vis; r++) {
    int i = s_top + r;
    int ry = LIST_Y0 + r * ROW_H;
    if (y >= ry - 3 && y < ry + ROW_H - 3) {
      if (inRect(x, y, 8, ry - 3, 28, ROW_H)) {
        s_rooms[i].done = !s_rooms[i].done;   // checkbox
      } else {
        selectRoom(i);                        // sweep this room now
      }
      g_dirty = true;
      return true;
    }
  }
  if (inRect(x, y, 12, 206, 140, 28)) {       // Add room
    uiKeyboardStart(WdKbPurpose::ADD_ROOM, "", -1);
    return true;
  }
  if (inRect(x, y, 168, 206, 140, 28)) {      // End sweep -> summary
    if (s_cur >= 0) {
      s_rooms[s_cur].findings += detectorsAlertTotal() - s_rooms[s_cur].alertsAtSel;
      s_cur = -1;
    }
    g_screen = WdScreen::SWEEP_SUMMARY;
    g_dirty = true;
    return true;
  }
  return false;
}

// ---- summary ------------------------------------------------------------------

void uiSweepSummaryDraw() {
  tft.fillScreen(WD_BG);
  drawSubHeader("SWEEP SUMMARY");

  WdSweepSummary s = detectorsSweepSummary();
  struct Row { const char* label; int n; };
  Row rows[] = {
    {"Possible cameras", s.cams},
    {"Trackers",         s.trackers},
    {"Evil twins",       s.twins},
    {"Karma responders", s.karma},
    {"Drones",           s.drones},
    {"New access points",s.newAps},
    {"New BT devices",   s.newBle},
  };
  const int n = sizeof(rows) / sizeof(rows[0]);
  for (int i = 0; i < n; i++) {
    int y = 46 + i * 22;
    tft.setTextColor(rows[i].n > 0 ? WD_ORANGE : WD_DIM, WD_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    char line[40];
    snprintf(line, sizeof(line), "%s: %d", rows[i].label, rows[i].n);
    tft.drawString(line, 160, y);
    tft.setTextDatum(TL_DATUM);
  }

  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Session discarded - home baseline intact", 160, 46 + n * 22 + 2);
  tft.setTextDatum(TL_DATUM);

  drawButton(12, 206, 296, 28, "Discard & exit", WD_GREEN, 1);
  tft.setTextSize(1);
}

bool uiSweepSummaryTouch(int x, int y) {
  if (y < 30 && x < 110) { g_screen = WdScreen::SWEEP_ROOMS; g_dirty = true; return true; }
  if (inRect(x, y, 12, 206, 296, 28)) { sweepEnd(); return true; }
  return false;
}
