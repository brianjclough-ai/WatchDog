// WatchDog v1 — dashboard implementation.
#include "ui_dashboard.h"
#include "ui_common.h"
#include "detectors.h"
#include "baseline.h"

#define TILE_W 100
#define TILE_H 54
#define TILE_XS 4
#define TILE_GX 6
#define TILE_YS 36
#define TILE_GY 6

static int tileX(int col) { return TILE_XS + col * (TILE_W + TILE_GX); }
static int tileY(int row) { return TILE_YS + row * (TILE_H + TILE_GY); }

struct TileDef {
  WdTile tile;
  const char* label;
};

static const TileDef kTiles[] = {
  {WdTile::WIFI,      "WiFi"},
  {WdTile::BLE,       "BLE"},
  {WdTile::JAM,       "Deauth"},
  {WdTile::EVILTWIN,  "EvilTwin"},
  {WdTile::TRACKERS,  "Trackers"},
  {WdTile::KARMA,     "Karma"},
  {WdTile::CAMERAS,   "Cameras"},
  {WdTile::DRONES,    "Drones"},
  {WdTile::KNOWN_TILE,"Known"},
};

// value text + whether the tile is in an alert state
static void tileValue(WdTile t, char* out, size_t n, bool& alert) {
  alert = false;
  switch (t) {
    case WdTile::WIFI:
      snprintf(out, n, "%d APs", g_tiles.wifiAps);
      alert = g_tiles.wifiNew > 0; break;
    case WdTile::BLE:
      snprintf(out, n, "%d dev", g_tiles.bleDevs);
      alert = g_tiles.bleNew > 0; break;
    case WdTile::JAM:
      snprintf(out, n, "%s", g_tiles.jamAlert ? "ALERT" : "OK");
      alert = g_tiles.jamAlert; break;
    case WdTile::EVILTWIN:
      snprintf(out, n, "%d", g_tiles.evilTwins);
      alert = g_tiles.evilTwins > 0; break;
    case WdTile::TRACKERS:
      snprintf(out, n, "%d", g_tiles.trackers);
      alert = g_tiles.trackers > 0; break;
    case WdTile::KARMA:
      snprintf(out, n, "%d", g_tiles.karma);
      alert = g_tiles.karma > 0; break;
    case WdTile::CAMERAS:
      snprintf(out, n, "%d", g_tiles.cameras);
      alert = g_tiles.cameras > 0; break;
    case WdTile::DRONES:
      snprintf(out, n, "%d", g_tiles.drones);
      alert = g_tiles.drones > 0; break;
    case WdTile::KNOWN_TILE:
      snprintf(out, n, "%d", g_tiles.known);
      break;
    default:
      snprintf(out, n, "-"); break;
  }
}

static void drawGear(int x, int y) {
  tft.drawCircle(x, y, 7, WD_DIM);
  tft.drawCircle(x, y, 3, WD_DIM);
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159f / 4.0f;
    tft.drawLine(x + (int)(7 * cos(a)), y + (int)(7 * sin(a)),
                 x + (int)(10 * cos(a)), y + (int)(10 * sin(a)), WD_DIM);
  }
}

void uiDashboardDraw() {
  tft.fillScreen(WD_BG);

  // header
  tft.fillCircle(16, 15, 8, WD_GREEN);
  tft.setTextColor(WD_GREEN, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(30, 8);
  tft.print("WatchDog");
  // Health: a scanner that stopped cycling must not look armed.
  const bool stalled = (g_mode != WdMode::LEARNING) && (wdNow() - g_lastCycleAt > 120);
  const char* mode;
  uint16_t mc;
  int pw;
  if (stalled)            { mode = "NO SCAN";  mc = WD_RED;    pw = 76; }
  else if (g_mode == WdMode::ARMED)  { mode = "ARMED";    mc = WD_GREEN;  pw = 64; }
  else if (g_mode == WdMode::SWEEP)  { mode = "SWEEP";    mc = WD_ORANGE; pw = 64; }
  else                    { mode = "LEARNING"; mc = WD_ORANGE; pw = 88; }
  int px = 228 - pw;
  tft.fillRoundRect(px, 5, pw, 20, 10, mc);
  tft.setTextColor(WD_BG, mc);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(mode, px + pw / 2, 16);
  tft.setTextDatum(TL_DATUM);
  char clk[8];
  wdClockStr(clk, sizeof(clk));
  tft.setTextColor(WD_TEXT, WD_BG);
  tft.setTextSize(2);
  tft.setCursor(232, 8);
  tft.print(clk);
  tft.setTextSize(1);
  drawGear(306, 15);
  tft.drawLine(0, 30, 320, 30, WD_LINE);

  // tiles
  for (int i = 0; i < 9; i++) {
    int col = i % 3, row = i / 3;
    int x = tileX(col), y = tileY(row);
    char val[16]; bool alert = false;
    tileValue(kTiles[i].tile, val, sizeof(val), alert);
    uint16_t border = alert ? WD_ORANGE : WD_LINE;
    tft.drawRoundRect(x, y, TILE_W, TILE_H, 8, border);
    tft.setTextColor(WD_DIM, WD_BG);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(kTiles[i].label, x + TILE_W / 2, y + 14);
    tft.setTextColor(alert ? WD_ORANGE : WD_TEXT, WD_BG);
    tft.setTextSize(2);
    tft.drawString(val, x + TILE_W / 2, y + 36);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
  }

  // footer
  tft.setTextColor(WD_DIM, WD_BG);
  tft.setCursor(4, 224);
  tft.print("FW v" WD_FW_VERSION);
  if (g_mode == WdMode::SWEEP) {
    tft.setTextColor(WD_ORANGE, WD_BG);
    tft.print("  session only");
  }
  if (!baselineStorageOk()) {
    tft.setTextColor(WD_RED, WD_BG);
    tft.print("  STORAGE!");
  }
  char al[24];
  snprintf(al, sizeof(al), "Alerts: %u >", (unsigned)detectorsAlertTotal());
  tft.setTextColor(detectorsAlertTotal() ? WD_ORANGE : WD_DIM, WD_BG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(al, 316, 228);
  tft.setTextDatum(TL_DATUM);
}

bool uiDashboardTouch(int x, int y) {
  // footer -> alert history
  if (inRect(x, y, 190, 214, 130, 26)) { g_screen = WdScreen::HISTORY; g_dirty = true; return true; }
  // gear -> settings
  if (inRect(x, y, 294, 3, 24, 24)) { g_screen = WdScreen::SETTINGS; g_dirty = true; return true; }
  for (int i = 0; i < 9; i++) {
    int col = i % 3, row = i / 3;
    if (inRect(x, y, tileX(col), tileY(row), TILE_W, TILE_H)) {
      if (kTiles[i].tile == WdTile::KNOWN_TILE) {
        g_screen = WdScreen::KNOWN;
      } else {
        g_detailTile = kTiles[i].tile;
        g_screen = WdScreen::DETAIL;
      }
      g_dirty = true;
      return true;
    }
  }
  return false;
}
