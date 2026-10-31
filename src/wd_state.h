// WatchDog v1 — shared types and globals.
// One place for the cross-module state so the UI, scanner, baseline and
// detectors all agree on shapes. No logic here.
#pragma once
#include <Arduino.h>

#define WD_FW_VERSION "1.1.0"

// Learning windows (seconds)
#define WD_LEARN_FULL_SECS  (24UL * 3600UL)  // full re-learn: one day/night cycle
#define WD_LEARN_ADD_SECS   (4UL * 3600UL)   // additive learn: a few hours

enum class WdScreen : uint8_t {
  DASHBOARD,
  DETAIL,     // per-tile detail list (g_detailTile selects the tile)
  KNOWN,      // baseline device list
  LEARNING,   // learning progress
  SETTINGS,
  CONFIRM,    // re-learn confirm dialog
  KEYBOARD,   // rename text entry
  HISTORY,    // alert history list
  ARMCONFIRM, // "arm with N devices?" review prompt
  SWEEP_ROOMS,   // sweep mode: room checklist
  SWEEP_SUMMARY, // sweep mode: findings summary
};

enum class WdMode : uint8_t {
  LEARNING,
  ARMED,
  SWEEP,   // rental sweep: RAM-only session, home baseline untouched
};

enum class WdTile : uint8_t {
  WIFI = 0,
  BLE,
  JAM,
  EVILTWIN,
  TRACKERS,
  KARMA,
  CAMERAS,
  DRONES,
  KNOWN_TILE,
  TILE_COUNT,
};

enum class WdAlertLevel : uint8_t {
  INFO = 0,
  WARNING,
};

// One baseline entry. Identity is the composite fingerprint (see
// baseline.*): MAC is the lookup key, fpHash + rssiBucket + radio do the
// merge-don't-alert scoring when a MAC rotates.
struct WdDevice {
  char     label[32];
  char     mac[18];      // "AA:BB:CC:DD:EE:FF"
  uint8_t  radio;        // 0 = wifi, 1 = ble
  uint32_t fpHash;       // payload-structure fingerprint
  int8_t   rssiBucket;   // RSSI / 12 dB
  uint32_t firstSeen;    // seconds since boot
  uint32_t lastSeen;     // seconds since boot
  bool     trusted;
};

// One popup-worthy event.
struct WdAlert {
  char         title[40];
  char         detail[64];
  char         reason[64];
  char         mac[18];   // device identity when the alert is about one
  uint8_t      radio;      // 0 = wifi, 1 = ble, 255 = not a single device
  uint32_t     fpHash;     // fingerprint (for Trust -> baselineAdd)
  int8_t       rssi;
  uint32_t     at;         // seconds since boot
  WdTile       tile;
  WdAlertLevel level;
};

// Live per-tile numbers for the dashboard.
struct WdTiles {
  int  wifiAps   = 0;
  int  wifiNew   = 0;
  int  bleDevs   = 0;
  int  bleNew    = 0;
  bool jamAlert  = false;
  int  jamFrames = 0;
  int  evilTwins = 0;
  int  trackers  = 0;
  int  karma     = 0;
  int  cameras   = 0;
  int  drones    = 0;
  int  known     = 0;
};

// Alert sensitivity (Settings screen).
// (SENS_ prefix: bare LOW/HIGH collide with Arduino's pin macros.)
enum class WdSensitivity : uint8_t { SENS_LOW = 0, SENS_MEDIUM, SENS_HIGH };

// --- globals (defined in main.cpp) ---
extern WdMode        g_mode;
extern WdScreen      g_screen;
extern WdTile        g_detailTile;
extern WdTiles       g_tiles;
extern WdSensitivity g_sens;
extern bool          g_alertPopup;   // an unacknowledged popup is showing
extern WdAlert       g_popup;        // the popup's content
extern bool          g_additiveLearn;// true when the current learning run is additive
extern uint32_t      g_learnEndsAt;  // seconds-since-boot when learning ends
extern uint32_t      g_bootMillis;
extern uint32_t      g_popupShownMs; // millis() when the current popup appeared
extern uint32_t      g_lastCycleAt;  // wdNow() when the last full scan cycle finished

// Alerts restored from flash after a reboot carry this timestamp
// (uptime seconds are meaningless across boots).
#define WD_AT_PREV_BOOT 0xFFFFFFFFu

// True when detectors should raise popups: ARMED, or SWEEP which behaves
// like ARMED but against a throwaway RAM session table (home baseline
// is never consulted and never written).
inline bool wdAlertsArmed() {
  return g_mode == WdMode::ARMED || g_mode == WdMode::SWEEP;
}

// On-screen keyboard purpose (ui_known's keyboard is shared).
enum class WdKbPurpose : uint8_t { RENAME_DEVICE, ADD_ROOM };
extern WdKbPurpose g_kbPurpose;

// seconds since boot
inline uint32_t wdNow() { return (uint32_t)(millis() / 1000UL); }

// Where "Back" returns to: the learning screen while learning, else the dashboard.
inline WdScreen wdHomeScreen() { return g_mode == WdMode::LEARNING ? WdScreen::LEARNING : WdScreen::DASHBOARD; }

// Set true whenever the screen needs a redraw (detectors, touch, clock).
extern bool g_dirty;

// "3h 12m" style age string. buf must hold >= 16 chars.
void wdAgeStr(uint32_t thenSec, char* buf, size_t bufSz);

// uptime clock "14:32" style. buf must hold >= 8 chars.
void wdClockStr(char* buf, size_t bufSz);
