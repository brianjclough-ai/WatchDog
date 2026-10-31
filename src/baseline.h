// WatchDog v1 — baseline: known-device store with composite fingerprinting.
// LittleFS JSON at /wd/baseline.json. Merge-don't-alert: a "new" MAC whose
// payload fingerprint + RSSI bucket matches a known entry is silently
// merged (lastSeen updated, no popup). Only a true stranger alerts.
#pragma once
#include "wd_state.h"
#include <stdint.h>

#define WD_BASELINE_MAX 200

void baselineInit();    // mount LittleFS, load or start fresh
void baselineSave();    // mark dirty (flushed lazily from the main loop)
void baselineTick();    // call every loop: rate-limits LittleFS writes

int        baselineCount();
WdDevice*  baselineAt(int idx);
int        baselineIndexOf(const WdDevice* d);  // index of a FindByMac pointer, or -1
WdDevice*  baselineFindByMac(const char* mac, uint8_t radio);

// Merge candidate: entry whose fingerprint matches (same radio,
// same fpHash, RSSI bucket within 1). Returns index or -1.
int baselineMatchFingerprint(uint32_t fpHash, int8_t rssiBucket, uint8_t radio);

// Add a fresh entry. Returns index or -1 when full.
int baselineAdd(const char* mac, uint8_t radio, uint32_t fpHash,
                int8_t rssi, const char* label);

void baselineTouch(int idx, int8_t rssi);  // refresh lastSeen
void baselineRemove(int idx);
void baselineRename(int idx, const char* label);
void baselineClear();

// Sweep session (v1.1.0): a RAM-only throwaway table. While active, every
// baseline*() call above transparently uses the session table; the LittleFS
// home baseline is never read for trust decisions and never written.
// detectors keep working unchanged. Trust during a sweep lands here.
void baselineSessionBegin();  // clear session table, route to it
void baselineSessionEnd();    // stop routing, discard the session
bool baselineInSession();

// Health + safety (v1.0.1)
bool baselineStorageOk();        // false when LittleFS could not mount or a save failed
int  baselineOverflowCount();    // devices that could not be recorded because the list is full
void baselineBackupForRelearn(); // copy the current baseline to /wd/prelearn.json

// FNV-1a over the STABLE parts of a BLE advert (AD structure types +
// data for mfg/service-data/service-UUIDs; skips the device name and
// anything that rotates with the MAC). Two adverts from the same
// physical device converge; different devices don't.
uint32_t fpHashBle(const uint8_t* payload, uint8_t len);

// Coarse RSSI bucket: 12 dB steps. A device sitting in one spot keeps
// its bucket across MAC rotations.
inline int8_t rssiBucketOf(int8_t rssi) { return (int8_t)(rssi / 12); }
