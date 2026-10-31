// WatchDog v1 — detector detail implementation.
#include "ui_detail.h"
#include "ui_common.h"
#include "detectors.h"

#define ROWS_PER_PAGE 4
#define ROW_H 44
#define LIST_Y 38

static int s_page = 0;
static WdTile s_tile = WdTile::WIFI;

void uiDetailReset() {
  s_page = 0;
  s_tile = g_detailTile;
}

void uiDetailDraw() {
  if (s_tile != g_detailTile) uiDetailReset();
  tft.fillScreen(WD_BG);
  drawSubHeader(detectorsTileTitle(g_detailTile));

  int n = detectorsDetailCount(g_detailTile);
  int pages = (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
  if (pages < 1) pages = 1;
  if (s_page >= pages) s_page = pages - 1;

  if (n == 0) {
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Nothing here", 160, 120);
    tft.setTextSize(1);
    tft.drawString("All quiet on this front.", 160, 145);
    tft.setTextDatum(TL_DATUM);
  } else {
    for (int r = 0; r < ROWS_PER_PAGE; r++) {
      int i = s_page * ROWS_PER_PAGE + r;
      if (i >= n) break;
      int y = LIST_Y + r * ROW_H;
      WdDetailRow row = detectorsDetailRow(g_detailTile, i);
      tft.drawLine(8, y + ROW_H - 4, 312, y + ROW_H - 4, WD_LINE);
      tft.setTextColor(WD_TEXT, WD_BG);
      tft.setTextSize(2);
      tft.setCursor(12, y);
      // size 2 fits ~26 chars; truncate defensively
      char l1[28];
      strncpy(l1, row.line1, 26); l1[26] = 0;
      tft.print(l1);
      tft.setTextColor(WD_DIM, WD_BG);
      tft.setTextSize(1);
      tft.setCursor(12, y + 22);
      tft.print(row.line2);
    }
  }

  // pager + clear
  tft.setTextColor(WD_DIM, WD_BG);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  if (pages > 1) {
    char pg[16];
    snprintf(pg, sizeof(pg), "%d / %d", s_page + 1, pages);
    tft.drawString(pg, 160, 222);
    drawButton(8, 206, 60, 28, "<", WD_DIM, 2);
    drawButton(252, 206, 60, 28, ">", WD_DIM, 2);
  }
  // Clear button for dismissable finding tiles
  if (g_detailTile == WdTile::EVILTWIN || g_detailTile == WdTile::KARMA ||
      g_detailTile == WdTile::CAMERAS) {
    drawButton(110, 206, 100, 28, "Clear", WD_ORANGE, 1);
  }
  tft.setTextDatum(TL_DATUM);
}

bool uiDetailTouch(int x, int y) {
  // header back zone
  if (y < 30 && x < 110) { g_screen = wdHomeScreen(); g_dirty = true; return true; }
  int n = detectorsDetailCount(g_detailTile);
  int pages = (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
  if (pages > 1) {
    if (inRect(x, y, 8, 206, 60, 28)) {
      s_page = (s_page + pages - 1) % pages; g_dirty = true; return true;
    }
    if (inRect(x, y, 252, 206, 60, 28)) {
      s_page = (s_page + 1) % pages; g_dirty = true; return true;
    }
  }
  if ((g_detailTile == WdTile::EVILTWIN || g_detailTile == WdTile::KARMA ||
       g_detailTile == WdTile::CAMERAS) &&
      inRect(x, y, 110, 206, 100, 28)) {
    detectorsClearTile(g_detailTile);
    g_screen = wdHomeScreen();
    g_dirty = true;
    return true;
  }
  return false;
}
