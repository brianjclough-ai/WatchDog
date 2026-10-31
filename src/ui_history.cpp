// WatchDog v1.0.1 — alert history screen.
#include "ui_history.h"
#include "ui_common.h"
#include "detectors.h"
#include <string.h>

#define HROWS   4
#define HROW_H  42
#define HLIST_Y 36

static int s_page = 0;

void uiHistoryReset() { s_page = 0; }

void uiHistoryDraw() {
  tft.fillScreen(WD_BG);
  char title[24];
  snprintf(title, sizeof(title), "ALERTS: %u", (unsigned)detectorsAlertTotal());
  drawSubHeader(title);

  int n = detectorsHistoryCount();
  int pages = (n + HROWS - 1) / HROWS;
  if (pages < 1) pages = 1;
  if (s_page >= pages) s_page = pages - 1;

  if (n == 0) {
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No alerts yet", 160, 120);
    tft.setTextDatum(TL_DATUM);
  } else {
    for (int r = 0; r < HROWS; r++) {
      int i = s_page * HROWS + r;
      if (i >= n) break;
      const WdAlert* a = detectorsHistoryAt(i);
      if (!a) break;
      int y = HLIST_Y + r * HROW_H;
      char age[16];
      wdAgeStr(a->at, age, sizeof(age));
      tft.drawLine(8, y + HROW_H - 3, 312, y + HROW_H - 3, WD_LINE);

      char l1[26];
      strncpy(l1, a->title, 25); l1[25] = 0;
      tft.setTextColor(a->level == WdAlertLevel::WARNING ? WD_ORANGE : WD_GREEN, WD_BG);
      tft.setTextSize(2);
      tft.setCursor(8, y);
      tft.print(l1);

      char l2[54];
      strncpy(l2, a->detail, 53); l2[53] = 0;
      tft.setTextSize(1);
      tft.setTextColor(WD_TEXT, WD_BG);
      tft.setCursor(8, y + 18);
      tft.print(l2);

      char l3[54];
      if (a->at == WD_AT_PREV_BOOT)
        snprintf(l3, sizeof(l3), "%.34s  RSSI %d  (%s)", a->reason, (int)a->rssi, age);
      else
        snprintf(l3, sizeof(l3), "%.34s  RSSI %d  %s ago", a->reason, (int)a->rssi, age);
      tft.setTextColor(WD_DIM, WD_BG);
      tft.setCursor(8, y + 29);
      tft.print(l3);
    }
  }

  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextDatum(MC_DATUM);
  if (pages > 1) {
    char pg[16];
    snprintf(pg, sizeof(pg), "%d / %d", s_page + 1, pages);
    tft.drawString(pg, 160, 222);
    drawButton(8, 206, 60, 28, "<", WD_DIM, 2);
    drawButton(252, 206, 60, 28, ">", WD_DIM, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

bool uiHistoryTouch(int x, int y) {
  if (y < 30 && x < 110) { g_screen = wdHomeScreen(); g_dirty = true; return true; }
  int n = detectorsHistoryCount();
  int pages = (n + HROWS - 1) / HROWS;
  if (pages > 1) {
    if (inRect(x, y, 8, 206, 60, 28)) { s_page = (s_page + pages - 1) % pages; g_dirty = true; return true; }
    if (inRect(x, y, 252, 206, 60, 28)) { s_page = (s_page + 1) % pages; g_dirty = true; return true; }
  }
  for (int r = 0; r < HROWS; r++) {
    int i = s_page * HROWS + r;
    if (i >= n) break;
    int ry = HLIST_Y + r * HROW_H;
    if (inRect(x, y, 0, ry, 320, HROW_H)) {
      const WdAlert* a = detectorsHistoryAt(i);
      if (a && a->tile != WdTile::KNOWN_TILE && a->tile != WdTile::TILE_COUNT) {
        g_detailTile = a->tile;
        g_screen = WdScreen::DETAIL;
        g_dirty = true;
      }
      return true;
    }
  }
  return false;
}
