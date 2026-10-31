// WatchDog v1 — detectors: one function family per dashboard tile.
// Feeds: BLE adverts + WiFi AP sweeps + promiscuous beacons/probe-resps
// from the scan layer. Applies the TwinWatch engine (evil-twin + karma),
// tracker persistence scoring, camera OUI hits, drone Remote ID, and the
// deauth-flood (JamDetect) counter. Raises popups only in ARMED mode.
#pragma once
#include "wd_state.h"
#include <stdint.h>

void detectorsInit();

// Scan-layer feeds:
void detectorsNoteBle(const char* mac, int8_t rssi,
                      const uint8_t* payload, uint8_t len);
void detectorsNoteAp(const char* ssid, const uint8_t* bssidRaw,
                     int8_t rssi, uint8_t channel, bool encrypted);
// Promiscuous-layer feeds (called while draining the scan ring):
void detectorsNoteBeacon(const uint8_t* bssid, const char* ssid,
                         bool encrypted, uint32_t ieHash);
void detectorsNoteProbeResp(const uint8_t* bssid, const char* ssid);
// End of one promiscuous listen window:
void detectorsNoteDeauthWindow(int frames);
// End of one full scan cycle (BLE + WiFi + listen):
void detectorsCycleDone();

// Detail-screen support:
struct WdDetailRow { char line1[40]; char line2[56]; };
int         detectorsDetailCount(WdTile t);
WdDetailRow detectorsDetailRow(WdTile t, int i);
const char* detectorsTileTitle(WdTile t);
void        detectorsClearTile(WdTile t);  // dismiss a tile's findings

// Raise the dashboard popup (latest wins). No-op unless ARMED.
void detectorsRaiseAlert(const char* title, const char* detail,
                         const char* reason, int8_t rssi,
                         WdTile tile, WdAlertLevel level);
// Same, but tagged with a device identity so Trust can baseline it.
// Pass radio = 0 (wifi) or 1 (ble).
void detectorsRaiseDeviceAlert(const char* title, const char* detail,
                               const char* reason, const char* mac,
                               uint8_t radio, uint32_t fpHash, int8_t rssi,
                               WdTile tile, WdAlertLevel level);
// Snooze alerts for one MAC for an hour (the Watch button).
void detectorsSnooze(const char* mac);

// Housekeeping, call every loop: persists the twin baseline and alert history.
// Skipped entirely while a sweep is active (home flash files stay untouched).
void detectorsTick();
// Forget the remembered SSID->AP pairings (used by full re-learn).
void detectorsResetTwinBaseline();

// Sweep mode (v1.1.0): rental/Airbnb checks. Findings counted per category
// for the end-of-sweep summary; everything is RAM-only and discarded on exit.
struct WdSweepSummary {
  int cams, trackers, twins, karma, drones, newAps, newBle;
};
void detectorsSweepBegin();   // park home twin pairings, clear tables + counters
void detectorsSweepEnd();     // restore home pairings, discard sweep findings
WdSweepSummary detectorsSweepSummary();

// Popup queue + history (v1.0.1). Every alert is recorded in a RAM history
// (last 16). If a popup is already showing, new alerts wait in a short queue
// instead of overwriting it. Call after the popup is dismissed.
void detectorsAlertDismissed();
int  detectorsHistoryCount();
const WdAlert* detectorsHistoryAt(int i);   // 0 = newest
uint32_t detectorsAlertTotal();             // alerts raised since boot
