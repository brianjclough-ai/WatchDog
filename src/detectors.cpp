// WatchDog v1 — detectors implementation.
#include "detectors.h"
#include "baseline.h"
#include "twin_watch.h"
#include "remote_id.h"
#include <string.h>
#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
#include <ArduinoJson.h>
#define WD_HAVE_FS 1
#endif

// ---- small tables -------------------------------------------------------

#define TRK_MAX   16
#define KARMA_MAX 12
#define TWIN_MAX  12
#define CAM_MAX   12
#define DRONE_MAX 6
#define SEEN_MAX  64
#define APSEEN_MAX 48
#define TRK_GAP_CYCLES 20   // a tag unseen this many cycles (~10 min) starts counting again
#define REALERT_SECS 900    // twin/karma: same finding re-alerts at most every 15 min

struct TrackerRec {
  uint32_t chunkHash;
  char     mac[18];
  int8_t   rssi;
  uint32_t firstSeen;
  uint8_t  cycles;
  uint32_t lastCycle;
  bool     watching;
  bool     alerted;
};

struct KarmaRec  { char bssid[18]; char ssid[33]; uint32_t at; };
struct TwinRec   { char ssid[33]; char bssid[18]; uint8_t verdict; uint32_t at; };
struct CamRec    { char bssid[18]; char vendor[20]; int8_t rssi; uint32_t at; };
struct DroneRec  { char mac[18]; RemoteId::Info info; uint32_t lastCycle; bool alerted; };
struct SeenRec   { char mac[18]; uint32_t fp; int8_t rssi; uint8_t cycles; uint32_t lastCycle; bool tracker; bool alerted; };
struct ApSeenRec { char bssid[18]; char ssid[33]; int8_t rssi; uint8_t ch; bool enc; uint32_t lastCycle; bool isNew; bool alerted; };

static TrackerRec s_trk[TRK_MAX];   static int s_trkN = 0;
static KarmaRec   s_karma[KARMA_MAX]; static int s_karmaN = 0;
static TwinRec    s_twin[TWIN_MAX];  static int s_twinN = 0;
static CamRec     s_cam[CAM_MAX];    static int s_camN = 0;
static DroneRec   s_drone[DRONE_MAX];static int s_droneN = 0;
static SeenRec    s_seen[SEEN_MAX];  static int s_seenN = 0;
static ApSeenRec  s_aps[APSEEN_MAX]; static int s_apsN = 0;

// Table slot for a new record: next free one, or when full the entry that
// was seen longest ago (tables never silently stop recording).
template <typename T>
static T* slotFor(T* arr, int& n, int cap) {
  if (n < cap) return &arr[n++];
  int best = 0;
  for (int i = 1; i < cap; i++)
    if (arr[i].lastCycle < arr[best].lastCycle) best = i;
  return &arr[best];
}

static TwinWatch s_tw;
static TwinWatch s_twBackup;   // home pairings, parked here while a sweep runs
static WdSweepSummary s_sweep; // v1.1.0: findings counted during the current sweep
#ifdef WD_HAVE_FS
static void twinLoad();
static void alertsLoad();
#endif
static uint32_t  s_cycle = 0;
static uint8_t   s_quietWindows = 0;

// "Watch" snoozes: MAC -> unix-ish expiry (seconds since boot).
#define SNOOZE_MAX 16
struct SnoozeRec { char mac[18]; uint32_t until; };
static SnoozeRec s_snooze[SNOOZE_MAX];
static int s_snoozeN = 0;

static bool snoozed(const char* mac) {
  if (!mac || !mac[0]) return false;
  uint32_t now = wdNow();
  for (int i = 0; i < s_snoozeN; i++)
    if (strcasecmp(s_snooze[i].mac, mac) == 0)
      return s_snooze[i].until > now;
  return false;
}

void detectorsSnooze(const char* mac) {
  if (!mac || !mac[0]) return;
  for (int i = 0; i < s_snoozeN; i++)
    if (strcasecmp(s_snooze[i].mac, mac) == 0) {
      s_snooze[i].until = wdNow() + 3600;
      return;
    }
  if (s_snoozeN < SNOOZE_MAX) {
    strlcpy(s_snooze[s_snoozeN].mac, mac, 18);
    s_snooze[s_snoozeN].until = wdNow() + 3600;
    s_snoozeN++;
  }
}

// Camera OUI starter list. A hit means "this access point's hardware maker
// also sells cameras" - a lead to investigate, never proof. Entries verified
// against the IEEE OUI registry (standards-oui.ieee.org) on 2026-10-11:
// 13 of 14 from-memory entries confirmed; A4:DA:22 ("Wyze") was wrong -
// the registry assigns it to "IEEE Registration Authority", so it was removed.
// Only devices that beacon as an access point are visible here; a camera that
// simply joins your Wi-Fi as a client never transmits a beacon.
struct CamOui { uint8_t b[3]; const char* vendor; };
static const CamOui kCamOuis[] = {
  {{0x2C, 0xAA, 0x8E}, "Wyze"},
  // NOTE: A4:DA:22 was listed as Wyze from memory but the IEEE registry
  // assigns it to "IEEE Registration Authority" (not Wyze) - removed 2026-10-11.
  {{0x18, 0xB4, 0x30}, "Google/Nest"},
  {{0x44, 0x19, 0xB6}, "Hikvision"},
  {{0x4C, 0xBD, 0x8F}, "Hikvision"},
  {{0x54, 0xC4, 0x15}, "Hikvision"},
  {{0xBC, 0xAD, 0x28}, "Hikvision"},
  {{0xC0, 0x56, 0xE3}, "Hikvision"},
  {{0x3C, 0xEF, 0x8C}, "Dahua"},
  {{0x4C, 0x11, 0xBF}, "Dahua"},
  {{0x90, 0x02, 0xA9}, "Dahua"},
  {{0x9C, 0x8E, 0xCD}, "Amcrest"},
  {{0xEC, 0x71, 0xDB}, "Reolink"},
  {{0xAC, 0xCC, 0x8E}, "Axis"},
};
static const int kCamOuiN = sizeof(kCamOuis) / sizeof(kCamOuis[0]);

// Compares ignoring the locally-administered bit (multi-SSID APs set it).
static const char* camOuiLookup(const uint8_t* bssid) {
  for (int i = 0; i < kCamOuiN; i++)
    if ((bssid[0] & ~0x02) == (kCamOuis[i].b[0] & ~0x02) &&
        bssid[1] == kCamOuis[i].b[1] && bssid[2] == kCamOuis[i].b[2])
      return kCamOuis[i].vendor;
  return nullptr;
}

// ---- helpers ------------------------------------------------------------

static void macStr(const uint8_t* raw, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
}

static uint8_t trackerCyclesNeeded() {
  if (g_mode == WdMode::SWEEP) return 2;   // sweep always runs hot
  switch (g_sens) {
    case WdSensitivity::SENS_LOW:    return 5;
    case WdSensitivity::SENS_HIGH:   return 2;
    default:                    return 3;
  }
}
static uint8_t persistCyclesNeeded() {
  if (g_mode == WdMode::SWEEP) return 3;
  switch (g_sens) {
    case WdSensitivity::SENS_LOW:    return 8;
    case WdSensitivity::SENS_HIGH:   return 3;
    default:                    return 5;
  }
}
static int jamFramesNeeded() {
  if (g_mode == WdMode::SWEEP) return 15;
  switch (g_sens) {
    case WdSensitivity::SENS_LOW:    return 60;
    case WdSensitivity::SENS_HIGH:   return 15;
    default:                    return 30;
  }
}

// FindMy identity: Apple company 0x004C, subtype 0x12, length 0x19,
// then the 25-byte chunk that is stable per tag for ~a day.
static const uint8_t* findMyChunk(const uint8_t* p, uint8_t len) {
  for (uint8_t i = 0; i + 4 + 25 <= len; i++) {
    if (p[i] == 0x4C && p[i + 1] == 0x00 && p[i + 2] == 0x12 && p[i + 3] == 0x19)
      return p + i + 4;
  }
  return nullptr;
}

static uint32_t fnv1a(const uint8_t* d, uint8_t n) {
  uint32_t h = 2166136261u;
  for (uint8_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
  return h;
}

void detectorsInit() {
  memset(s_trk, 0, sizeof(s_trk));   s_trkN = 0;
  s_karmaN = s_twinN = s_camN = s_droneN = s_seenN = s_apsN = 0;
  s_cycle = 0; s_quietWindows = 0;
  memset(&s_tw, 0, sizeof(s_tw));
#ifdef WD_HAVE_FS
  twinLoad();     // remembered SSID -> AP pairings survive reboots
  alertsLoad();   // so does the alert history
#endif
}

// ---- alert queue + history ------------------------------------------------

#define HIST_MAX 16
#define PEND_MAX 4
static WdAlert  s_hist[HIST_MAX];
static int      s_histHead = 0, s_histN = 0;
static uint32_t s_histTotal = 0;
static WdAlert  s_pend[PEND_MAX];
static int      s_pendN = 0;
static bool     s_alertsDirty = false;

static void pushAlert(const WdAlert& a) {
  s_hist[s_histHead] = a;
  s_histHead = (s_histHead + 1) % HIST_MAX;
  if (s_histN < HIST_MAX) s_histN++;
  s_histTotal++;
  s_alertsDirty = true;
  if (!g_alertPopup) {
    g_popup = a;
    g_alertPopup = true;
    g_popupShownMs = millis();
  } else {
    if (s_pendN == PEND_MAX) {  // drop the oldest waiting one (still in history)
      for (int i = 1; i < PEND_MAX; i++) s_pend[i - 1] = s_pend[i];
      s_pendN--;
    }
    s_pend[s_pendN++] = a;
  }
  g_dirty = true;
}

void detectorsAlertDismissed() {
  if (s_pendN > 0) {
    g_popup = s_pend[0];
    for (int i = 1; i < s_pendN; i++) s_pend[i - 1] = s_pend[i];
    s_pendN--;
    g_alertPopup = true;
    g_popupShownMs = millis();
  }
  g_dirty = true;
}

int detectorsHistoryCount() { return s_histN; }
uint32_t detectorsAlertTotal() { return s_histTotal; }
const WdAlert* detectorsHistoryAt(int i) {
  if (i < 0 || i >= s_histN) return nullptr;
  return &s_hist[(s_histHead - 1 - i + 2 * HIST_MAX) % HIST_MAX];
}

void detectorsRaiseAlert(const char* title, const char* detail,
                         const char* reason, int8_t rssi,
                         WdTile tile, WdAlertLevel level) {
  if (!wdAlertsArmed()) return;
  WdAlert a;
  memset(&a, 0, sizeof(a));
  strlcpy(a.title, title, sizeof(a.title));
  strlcpy(a.detail, detail, sizeof(a.detail));
  strlcpy(a.reason, reason, sizeof(a.reason));
  a.radio = 255;
  a.rssi  = rssi;
  a.at    = wdNow();
  a.tile  = tile;
  a.level = level;
  pushAlert(a);
}

void detectorsRaiseDeviceAlert(const char* title, const char* detail,
                               const char* reason, const char* mac,
                               uint8_t radio, uint32_t fpHash, int8_t rssi,
                               WdTile tile, WdAlertLevel level) {
  if (!wdAlertsArmed()) return;
  if (snoozed(mac)) return;
  WdAlert a;
  memset(&a, 0, sizeof(a));
  strlcpy(a.title, title, sizeof(a.title));
  strlcpy(a.detail, detail, sizeof(a.detail));
  strlcpy(a.reason, reason, sizeof(a.reason));
  strlcpy(a.mac, mac ? mac : "", sizeof(a.mac));
  a.radio  = radio;
  a.fpHash = fpHash;
  a.rssi   = rssi;
  a.at     = wdNow();
  a.tile   = tile;
  a.level  = level;
  pushAlert(a);
}

// ---- persistence: twin baseline + alert history -------------------------------
//
// The twin engine learns "SSID -> first AP heard" in RAM; without this, every
// reboot re-learned it from scratch, so a rogue already on air at boot became
// the baseline. Both files carry a checksum and are written tmp-then-rename,
// at most once a minute, and only when their content changed.

#ifdef WD_HAVE_FS
#define TWIN_FILE      "/wd/twins.bin"
#define TWIN_TMP       "/wd/twins.tmp"
#define ALERTS_FILE    "/wd/alerts.json"
#define ALERTS_TMP     "/wd/alerts.tmp"
#define TWIN_MAGIC     0x31544457u   // "WDT1"

static uint32_t s_twinSavedHash = 0;
static uint32_t s_lastPersistMs = 0;
static uint32_t s_lastTwinSaveMs = 0;

static uint32_t fnvBytes(const uint8_t* d, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
  return h;
}

static uint32_t twinHash() {
  uint32_t h = fnvBytes((const uint8_t*)&s_tw.count, sizeof(s_tw.count));
  for (int i = 0; i < s_tw.count; i++) {
    // Hash fields, not raw structs (padding bytes are undefined).
    h ^= fnvBytes((const uint8_t*)s_tw.aps[i].ssid, strlen(s_tw.aps[i].ssid)); h *= 16777619u;
    h ^= fnvBytes(s_tw.aps[i].bssid, 6); h *= 16777619u;
    h ^= s_tw.aps[i].encrypted; h *= 16777619u;
    h ^= s_tw.aps[i].ieHash; h *= 16777619u;
  }
  return h;
}

// Record format: magic, sizeof(ApEntry), count, next, entries..., fnv of all prior bytes.
static void twinLoad() {
  File f = LittleFS.open(TWIN_FILE, "r");
  if (!f) return;
  uint32_t magic = 0, esz = 0; uint8_t cnt = 0, nxt = 0;
  bool ok = f.read((uint8_t*)&magic, 4) == 4 && f.read((uint8_t*)&esz, 4) == 4 &&
            f.read(&cnt, 1) == 1 && f.read(&nxt, 1) == 1 &&
            magic == TWIN_MAGIC && esz == sizeof(TwinWatch::ApEntry) &&
            cnt <= TwinWatch::AP_CAP && nxt < TwinWatch::AP_CAP;
  static TwinWatch tmp;   // static: keeps ~1.5 KB off the stack
  if (ok) {
    memset(&tmp, 0, sizeof(tmp));
    size_t n = (size_t)cnt * sizeof(TwinWatch::ApEntry);
    ok = f.read((uint8_t*)tmp.aps, n) == (int)n;
    uint32_t sum = 0;
    if (ok) ok = f.read((uint8_t*)&sum, 4) == 4;
    if (ok) {
      uint32_t h = fnvBytes((const uint8_t*)&magic, 4);
      h ^= fnvBytes((const uint8_t*)&esz, 4); h *= 16777619u;
      h ^= fnvBytes(&cnt, 1); h *= 16777619u;
      h ^= fnvBytes(&nxt, 1); h *= 16777619u;
      h ^= fnvBytes((const uint8_t*)tmp.aps, n);
      ok = (h == sum);
    }
  }
  f.close();
  if (!ok) return;
  for (int i = 0; i < cnt; i++) tmp.aps[i].ssid[32] = 0;
  tmp.count = cnt; tmp.next = nxt;
  memcpy(&s_tw, &tmp, sizeof(s_tw));
  s_twinSavedHash = twinHash();
}

static void twinSave() {
  File f = LittleFS.open(TWIN_TMP, "w");
  if (!f) return;
  uint32_t magic = TWIN_MAGIC, esz = sizeof(TwinWatch::ApEntry);
  uint8_t cnt = s_tw.count, nxt = s_tw.next;
  size_t n = (size_t)cnt * sizeof(TwinWatch::ApEntry);
  uint32_t h = fnvBytes((const uint8_t*)&magic, 4);
  h ^= fnvBytes((const uint8_t*)&esz, 4); h *= 16777619u;
  h ^= fnvBytes(&cnt, 1); h *= 16777619u;
  h ^= fnvBytes(&nxt, 1); h *= 16777619u;
  h ^= fnvBytes((const uint8_t*)s_tw.aps, n);
  f.write((const uint8_t*)&magic, 4); f.write((const uint8_t*)&esz, 4);
  f.write(&cnt, 1); f.write(&nxt, 1);
  f.write((const uint8_t*)s_tw.aps, n);
  f.write((const uint8_t*)&h, 4);
  bool ok = (f.getWriteError() == 0);
  f.close();
  if (!ok) { LittleFS.remove(TWIN_TMP); return; }
  LittleFS.remove(TWIN_FILE);
  if (LittleFS.rename(TWIN_TMP, TWIN_FILE)) s_twinSavedHash = twinHash();
}

static void alertsLoad() {
  File f = LittleFS.open(ALERTS_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  JsonArray arr = doc.as<JsonArray>();   // oldest first
  for (JsonObject o : arr) {
    WdAlert a;
    memset(&a, 0, sizeof(a));
    strlcpy(a.title,  o["t"]  | "", sizeof(a.title));
    strlcpy(a.detail, o["d"]  | "", sizeof(a.detail));
    strlcpy(a.reason, o["r"]  | "", sizeof(a.reason));
    strlcpy(a.mac,    o["m"]  | "", sizeof(a.mac));
    a.radio  = o["rd"] | 255;
    a.fpHash = o["fp"] | 0;
    a.rssi   = o["rs"] | 0;
    a.tile   = (WdTile)(o["tl"] | 0);
    if ((uint8_t)a.tile >= (uint8_t)WdTile::TILE_COUNT) a.tile = WdTile::WIFI;
    a.level  = (WdAlertLevel)(o["lv"] | 0);
    a.at     = WD_AT_PREV_BOOT;
    s_hist[s_histHead] = a;
    s_histHead = (s_histHead + 1) % HIST_MAX;
    if (s_histN < HIST_MAX) s_histN++;
  }
  s_histTotal = (uint32_t)s_histN;
}

static void alertsSave() {
  File f = LittleFS.open(ALERTS_TMP, "w");
  if (!f) return;
  f.print('[');
  for (int i = s_histN - 1; i >= 0; i--) {          // oldest first
    const WdAlert* a = detectorsHistoryAt(i);
    if (!a) continue;
    if (i != s_histN - 1) f.print(',');
    JsonDocument o;
    o["t"] = a->title; o["d"] = a->detail; o["r"] = a->reason; o["m"] = a->mac;
    o["rd"] = a->radio; o["fp"] = a->fpHash; o["rs"] = a->rssi;
    o["tl"] = (uint8_t)a->tile; o["lv"] = (uint8_t)a->level;
    serializeJson(o, f);
  }
  f.print(']');
  bool ok = (f.getWriteError() == 0);
  f.close();
  if (!ok) { LittleFS.remove(ALERTS_TMP); return; }
  LittleFS.remove(ALERTS_FILE);
  if (LittleFS.rename(ALERTS_TMP, ALERTS_FILE)) s_alertsDirty = false;
}
#endif  // WD_HAVE_FS

void detectorsTick() {
#ifdef WD_HAVE_FS
  if (g_mode == WdMode::SWEEP) return;  // sweep: home flash files stay untouched
  uint32_t now = millis();
  if (now - s_lastPersistMs < 60000UL) return;
  s_lastPersistMs = now;
  if (s_alertsDirty) alertsSave();
  // Twin baseline: only when it changed, and at most every 10 minutes.
  if (twinHash() != s_twinSavedHash &&
      (s_lastTwinSaveMs == 0 || now - s_lastTwinSaveMs >= 600000UL)) {
    twinSave();
    s_lastTwinSaveMs = now ? now : 1;
  }
#endif
}

void detectorsResetTwinBaseline() {
  memset(&s_tw, 0, sizeof(s_tw));
#ifdef WD_HAVE_FS
  LittleFS.remove(TWIN_FILE);
  s_twinSavedHash = twinHash();
#endif
  s_twinN = 0; s_karmaN = 0;
}

// ---- sweep mode (v1.1.0) ------------------------------------------------------
// A sweep runs every detector against a throwaway RAM session: the twin
// engine's home pairings are parked aside (a rental must not pollute them),
// all finding tables start empty, and nothing is written to flash
// (detectorsTick is gated off in SWEEP). On exit the home pairings are
// restored verbatim and the sweep's findings are discarded.

void detectorsSweepBegin() {
  memcpy(&s_twBackup, &s_tw, sizeof(s_tw));
  memset(&s_tw, 0, sizeof(s_tw));
  s_trkN = s_karmaN = s_twinN = s_camN = s_droneN = s_seenN = s_apsN = 0;
  s_snoozeN = 0;
  memset(&s_sweep, 0, sizeof(s_sweep));
}

void detectorsSweepEnd() {
  memcpy(&s_tw, &s_twBackup, sizeof(s_tw));  // home pairings back, byte-identical
  s_trkN = s_karmaN = s_twinN = s_camN = s_droneN = s_seenN = s_apsN = 0;
  s_snoozeN = 0;
}

WdSweepSummary detectorsSweepSummary() { return s_sweep; }

// ---- BLE ---------------------------------------------------------------

void detectorsNoteBle(const char* mac, int8_t rssi,
                      const uint8_t* payload, uint8_t len) {
  if (!mac || !payload) return;

  // 1) Known MAC -> touch, done.
  WdDevice* known = baselineFindByMac(mac, 1);
  if (known) baselineTouch(baselineIndexOf(known), rssi);

  // 2) Merge-don't-alert: same fingerprint, rotated MAC.
  uint32_t fp = fpHashBle(payload, len);
  int mIdx = -1;
  if (!known) mIdx = baselineMatchFingerprint(fp, rssiBucketOf(rssi), 1);
  if (mIdx >= 0) { baselineTouch(mIdx, rssi); known = baselineAt(mIdx); }

  // 3) Track in the per-cycle seen table (for counts + persistence).
  SeenRec* rec = nullptr;
  for (int i = 0; i < s_seenN; i++)
    if (strcasecmp(s_seen[i].mac, mac) == 0) { rec = &s_seen[i]; break; }
  if (!rec) {
    rec = slotFor(s_seen, s_seenN, SEEN_MAX);
    memset(rec, 0, sizeof(*rec));
    strlcpy(rec->mac, mac, sizeof(rec->mac));
    rec->fp = fp; rec->cycles = 0; rec->tracker = false; rec->alerted = false;
    rec->lastCycle = 0xFFFFFFFF;  // so the first sighting counts as cycle 1
  }
  if (rec) {
    if (rec->lastCycle != s_cycle) { rec->cycles++; rec->lastCycle = s_cycle; }
    rec->rssi = rssi;
  }

  // 4) Drone Remote ID check (BLE legacy form).
  if (RemoteId::present(payload, len)) {
    DroneRec* dr = nullptr;
    for (int i = 0; i < s_droneN; i++)
      if (strcasecmp(s_drone[i].mac, mac) == 0) { dr = &s_drone[i]; break; }
    if (!dr) {
      dr = slotFor(s_drone, s_droneN, DRONE_MAX);
      dr->lastCycle = s_cycle;
      strlcpy(dr->mac, mac, sizeof(dr->mac));
      RemoteId::reset(dr->info);
      dr->alerted = false;
    }
    if (dr) {
      RemoteId::merge(payload, len, dr->info, millis());
      dr->lastCycle = s_cycle;
      if (!dr->alerted && wdAlertsArmed()) {
        dr->alerted = true;
        if (g_mode == WdMode::SWEEP) s_sweep.drones++;
        char d[64];
        if (dr->info.haveBasic && dr->info.serial[0])
          snprintf(d, sizeof(d), "ID %s", dr->info.serial);
        else
          snprintf(d, sizeof(d), "Remote ID signal, ID decoding...");
        detectorsRaiseAlert("Drone overhead", d,
                            "ASTM F3411 Remote ID broadcast",
                            rssi, WdTile::DRONES, WdAlertLevel::INFO);
      }
    }
  }

  // 5) Tracker-class check.
  const uint8_t* chunk = findMyChunk(payload, len);
  bool isTracker = (chunk != nullptr);
  if (rec) rec->tracker = rec->tracker || isTracker;

  if (known) return;  // known or merged: never alert.

  if (g_mode == WdMode::LEARNING) {
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%s %.5s", isTracker ? "Tag" : "BLE", mac + 12);
    baselineAdd(mac, 1, fp, rssi, lbl);
    return;
  }

  // ARMED: persistence scoring.
  if (isTracker && chunk) {
    uint32_t ch = fnv1a(chunk, 25);
    TrackerRec* t = nullptr;
    for (int i = 0; i < s_trkN; i++)
      if (s_trk[i].chunkHash == ch) { t = &s_trk[i]; break; }
    if (!t) {
      t = slotFor(s_trk, s_trkN, TRK_MAX);
      memset(t, 0, sizeof(*t));
      t->chunkHash = ch; t->firstSeen = wdNow();
      t->cycles = 0; t->watching = false; t->alerted = false;
      t->lastCycle = 0xFFFFFFFF;  // first sighting counts as cycle 1
      strlcpy(t->mac, mac, sizeof(t->mac));
    }
    if (t) {
      if (t->lastCycle != s_cycle) {
        // Sightings must be reasonably continuous: a tag glimpsed once a day
        // by a passer-by must not add up to an alert.
        if (t->lastCycle != 0xFFFFFFFF && s_cycle - t->lastCycle > TRK_GAP_CYCLES) t->cycles = 0;
        t->cycles++; t->lastCycle = s_cycle;
      }
      t->rssi = rssi;
      if (!t->watching && t->cycles >= trackerCyclesNeeded()) {
        t->watching = true;
        if (!t->alerted) {
          t->alerted = true;
          if (g_mode == WdMode::SWEEP) s_sweep.trackers++;
          char d[64];
          snprintf(d, sizeof(d), "Unknown tag, here %u min", (unsigned)((wdNow() - t->firstSeen) / 60));
          detectorsRaiseDeviceAlert("Possible tracker", d,
                              "FindMy-style tag keeps showing up",
                              mac, 1, fp, rssi,
                              WdTile::TRACKERS, WdAlertLevel::WARNING);
        }
      }
    }
    return;
  }

  // Unknown non-tracker BLE in ARMED mode.
  if (snoozed(mac)) return;
  if (rec && !rec->alerted) {
    // First sighting: info alert right away (one tap to trust).
    rec->alerted = true;
    if (g_mode == WdMode::SWEEP) s_sweep.newBle++;
    char d[64];
    snprintf(d, sizeof(d), "%.17s, RSSI %d", mac, (int)rssi);
    detectorsRaiseDeviceAlert("New Bluetooth device", d,
                              "Wasn't here during learning",
                              mac, 1, fp, rssi,
                              WdTile::BLE, WdAlertLevel::INFO);
  } else if (rec && rec->cycles >= persistCyclesNeeded() && !rec->tracker) {
    // It won't go away: camera or bug candidate. Throttled.
    static uint32_t s_lastPersistAlert = 0;
    if (wdNow() - s_lastPersistAlert > 3600) {
      s_lastPersistAlert = wdNow();
      char d[64];
      snprintf(d, sizeof(d), "%s, %u cycles, RSSI %d", mac, (unsigned)rec->cycles, (int)rssi);
      detectorsRaiseDeviceAlert("Persistent BLE device", d,
                          "Unknown advertiser won't go away",
                          mac, 1, fp, rssi,
                          WdTile::BLE, WdAlertLevel::INFO);
    }
  }
}

// ---- WiFi APs ------------------------------------------------------------

void detectorsNoteAp(const char* ssid, const uint8_t* bssidRaw,
                     int8_t rssi, uint8_t channel, bool encrypted) {
  if (!bssidRaw) return;
  char bssid[18];
  macStr(bssidRaw, bssid);

  WdDevice* known = baselineFindByMac(bssid, 0);
  if (known) baselineTouch(baselineIndexOf(known), rssi);

  ApSeenRec* rec = nullptr;
  for (int i = 0; i < s_apsN; i++)
    if (strcasecmp(s_aps[i].bssid, bssid) == 0) { rec = &s_aps[i]; break; }
  if (!rec) {
    rec = slotFor(s_aps, s_apsN, APSEEN_MAX);
    memset(rec, 0, sizeof(*rec));
    strlcpy(rec->bssid, bssid, sizeof(rec->bssid));
    rec->isNew = false;
  }
  if (rec) {
    strlcpy(rec->ssid, ssid && ssid[0] ? ssid : "(hidden)", sizeof(rec->ssid));
    rec->rssi = rssi; rec->ch = channel; rec->enc = encrypted;
    rec->lastCycle = s_cycle;
  }

  if (known) return;

  // Camera OUI check (runs in both modes; in learning it just labels).
  const char* camVendor = camOuiLookup(bssidRaw);
  if (g_mode == WdMode::LEARNING) {
    char lbl[32];
    if (camVendor) snprintf(lbl, sizeof(lbl), "%.10s cam? %.5s", camVendor, bssid + 12);
    else if (ssid && ssid[0]) snprintf(lbl, sizeof(lbl), "AP %.24s", ssid);
    else snprintf(lbl, sizeof(lbl), "AP %.5s", bssid + 12);
    int idx = baselineAdd(bssid, 0, 0, rssi, lbl);
    (void)idx;
    if (rec) rec->isNew = false;
    return;
  }

  // ARMED
  if (rec) rec->isNew = true;
  if (rec && rec->alerted) return;  // already told you about this one
  if (camVendor) {
    if (s_camN < CAM_MAX) {
      CamRec* c = &s_cam[s_camN++];
      strlcpy(c->bssid, bssid, sizeof(c->bssid));
      strlcpy(c->vendor, camVendor, sizeof(c->vendor));
      c->rssi = rssi; c->at = wdNow();
    }
    if (g_mode == WdMode::SWEEP) s_sweep.cams++;
    char d[64];
    snprintf(d, sizeof(d), "%.12s hardware at %.17s", camVendor, bssid);
    detectorsRaiseDeviceAlert("Possible camera", d,
                        "Known camera maker OUI, first sighting",
                        bssid, 0, 0, rssi,
                        WdTile::CAMERAS, WdAlertLevel::WARNING);
  } else {
    if (g_mode == WdMode::SWEEP) s_sweep.newAps++;
    char d[64];
    snprintf(d, sizeof(d), "%s (%.17s)", ssid && ssid[0] ? ssid : "hidden AP", bssid);
    detectorsRaiseDeviceAlert("New access point", d,
                        "Wasn't here during learning",
                        bssid, 0, 0, rssi,
                        WdTile::WIFI, WdAlertLevel::INFO);
  }
  if (rec) rec->alerted = true;
}

// ---- promiscuous feeds (TwinWatch engine) ---------------------------------

void detectorsNoteBeacon(const uint8_t* bssid, const char* ssid,
                         bool encrypted, uint32_t ieHash) {
  TwinWatch::Verdict v = s_tw.noteBeacon(bssid, ssid, encrypted, ieHash, 0);
  if (v == TwinWatch::Verdict::NONE || !wdAlertsArmed()) return;
  char bs[18]; macStr(bssid, bs);
  // The same rogue beacons ten times a second: record and alert once per
  // REALERT_SECS, otherwise the popup (and the table) would churn forever.
  TwinRec* t = nullptr;
  for (int i = 0; i < s_twinN; i++)
    if (strcmp(s_twin[i].bssid, bs) == 0 && strcmp(s_twin[i].ssid, ssid) == 0) { t = &s_twin[i]; break; }
  if (t) {
    if (wdNow() - t->at < REALERT_SECS) return;
  } else if (s_twinN < TWIN_MAX) {
    t = &s_twin[s_twinN++];
  } else {
    t = &s_twin[0];
    for (int i = 1; i < TWIN_MAX; i++) if (s_twin[i].at < t->at) t = &s_twin[i];
  }
  strlcpy(t->ssid, ssid, sizeof(t->ssid));
  strlcpy(t->bssid, bs, sizeof(t->bssid));
  t->verdict = (uint8_t)v; t->at = wdNow();
  if (g_mode == WdMode::SWEEP) s_sweep.twins++;
  char d[64];
  snprintf(d, sizeof(d), "\"%s\" also from %.17s", ssid, bs);
  detectorsRaiseAlert(v == TwinWatch::Verdict::EVIL_TWIN ? "Evil twin?" : "Suspect twin",
                      d, "Same network name, different hardware",
                      -60, WdTile::EVILTWIN, WdAlertLevel::WARNING);
}

void detectorsNoteProbeResp(const uint8_t* bssid, const char* ssid) {
  if (!wdAlertsArmed()) return;
  if (!s_tw.noteProbeResp(bssid, ssid)) return;
  char bs[18]; macStr(bssid, bs);
  KarmaRec* k = nullptr;
  for (int i = 0; i < s_karmaN; i++)
    if (strcmp(s_karma[i].bssid, bs) == 0 && strcmp(s_karma[i].ssid, ssid) == 0) { k = &s_karma[i]; break; }
  if (k) {
    if (wdNow() - k->at < REALERT_SECS) return;
  } else if (s_karmaN < KARMA_MAX) {
    k = &s_karma[s_karmaN++];
  } else {
    k = &s_karma[0];
    for (int i = 1; i < KARMA_MAX; i++) if (s_karma[i].at < k->at) k = &s_karma[i];
  }
  strlcpy(k->bssid, bs, sizeof(k->bssid));
  strlcpy(k->ssid, ssid, sizeof(k->ssid));
  k->at = wdNow();
  if (g_mode == WdMode::SWEEP) s_sweep.karma++;
  char d[64];
  snprintf(d, sizeof(d), "%.17s answering for \"%s\"", bs, ssid);
  detectorsRaiseAlert("Karma responder?", d,
                      "AP answers probes for networks it doesn't run",
                      -60, WdTile::KARMA, WdAlertLevel::WARNING);
}

void detectorsNoteDeauthWindow(int frames) {
  g_tiles.jamFrames = frames;
  if (frames >= jamFramesNeeded()) {
    s_quietWindows = 0;
    if (!g_tiles.jamAlert) {
      g_tiles.jamAlert = true;
      char d[64];
      snprintf(d, sizeof(d), "%d deauth/disassoc frames in one sweep", frames);
      detectorsRaiseAlert("Deauth flood?", d,
                          "Many disconnect frames - possible attack",
                          -50, WdTile::JAM, WdAlertLevel::WARNING);
    }
  } else {
    if (g_tiles.jamAlert && ++s_quietWindows >= 2) g_tiles.jamAlert = false;
  }
}

// ---- end of cycle ----------------------------------------------------------

void detectorsCycleDone() {
  s_cycle++;
  g_lastCycleAt = wdNow();

  int wifiN = 0, wifiNew = 0, bleN = 0, bleNew = 0;
  for (int i = 0; i < s_apsN; i++)
    if (s_aps[i].lastCycle == s_cycle - 1 || s_aps[i].lastCycle == s_cycle) {
      wifiN++; if (s_aps[i].isNew) wifiNew++;
    }
  for (int i = 0; i < s_seenN; i++)
    if (s_seen[i].lastCycle == s_cycle - 1 || s_seen[i].lastCycle == s_cycle) {
      bleN++;
      if (!baselineFindByMac(s_seen[i].mac, 1) &&
          baselineMatchFingerprint(s_seen[i].fp, rssiBucketOf(s_seen[i].rssi), 1) < 0)
        bleNew++;
    }

  int trk = 0;
  for (int i = 0; i < s_trkN; i++) {
    // A tag Brian trusted via the popup leaves the watch list.
    if (baselineFindByMac(s_trk[i].mac, 1)) { s_trk[i].watching = false; continue; }
    if (s_trk[i].watching) trk++;
  }
  int dr = 0;
  for (int i = 0; i < s_droneN; i++)
    if (s_drone[i].lastCycle + 2 >= s_cycle) dr++;

  g_tiles.wifiAps  = wifiN;
  g_tiles.wifiNew  = wifiNew;
  g_tiles.bleDevs  = bleN;
  g_tiles.bleNew   = bleNew;
  g_tiles.evilTwins= s_twinN;
  g_tiles.trackers = trk;
  g_tiles.karma    = s_karmaN;
  g_tiles.cameras  = s_camN;
  g_tiles.drones   = dr;
  g_tiles.known    = baselineCount();
  g_dirty = true;  // dashboard numbers changed
}

// ---- detail screens ---------------------------------------------------------

const char* detectorsTileTitle(WdTile t) {
  switch (t) {
    case WdTile::WIFI:     return "WI-FI NETWORKS";
    case WdTile::BLE:      return "BLUETOOTH";
    case WdTile::JAM:      return "DEAUTH WATCH";
    case WdTile::EVILTWIN: return "EVIL TWIN";
    case WdTile::TRACKERS: return "TRACKERS";
    case WdTile::KARMA:    return "KARMA";
    case WdTile::CAMERAS:  return "CAMERAS";
    case WdTile::DRONES:   return "DRONES";
    default:               return "DETAIL";
  }
}

int detectorsDetailCount(WdTile t) {
  switch (t) {
    case WdTile::WIFI:     return s_apsN;
    case WdTile::BLE:      return s_seenN;
    case WdTile::EVILTWIN: return s_twinN;
    case WdTile::TRACKERS: return s_trkN;
    case WdTile::KARMA:    return s_karmaN;
    case WdTile::CAMERAS:  return s_camN;
    case WdTile::DRONES:   return s_droneN;
    case WdTile::JAM:      return 1;
    default:               return 0;
  }
}

WdDetailRow detectorsDetailRow(WdTile t, int i) {
  WdDetailRow r; r.line1[0] = 0; r.line2[0] = 0;
  char age[16];
  switch (t) {
    case WdTile::WIFI:
      if (i < s_apsN) {
        snprintf(r.line1, sizeof(r.line1), "%s%s", s_aps[i].isNew ? "* " : "", s_aps[i].ssid);
        snprintf(r.line2, sizeof(r.line2), "RSSI %d  ch %d  %s", (int)s_aps[i].rssi,
                 s_aps[i].ch, s_aps[i].enc ? "secured" : "OPEN");
      }
      break;
    case WdTile::BLE:
      if (i < s_seenN) {
        WdDevice* k = baselineFindByMac(s_seen[i].mac, 1);
        snprintf(r.line1, sizeof(r.line1), "%s", k ? k->label : s_seen[i].mac);
        snprintf(r.line2, sizeof(r.line2), "RSSI %d  seen %u cycles%s",
                 (int)s_seen[i].rssi, (unsigned)s_seen[i].cycles,
                 s_seen[i].tracker ? "  TAG?" : "");
      }
      break;
    case WdTile::JAM:
      snprintf(r.line1, sizeof(r.line1), "%s", g_tiles.jamAlert ? "ALERT: deauth flood?" : "No flood seen");
      snprintf(r.line2, sizeof(r.line2), "%d deauth frames last sweep", g_tiles.jamFrames);
      break;
    case WdTile::EVILTWIN:
      if (i < s_twinN) {
        wdAgeStr(s_twin[i].at, age, sizeof(age));
        snprintf(r.line1, sizeof(r.line1), "%s \"%s\"",
                 s_twin[i].verdict == 2 ? "EVIL TWIN" : "suspect", s_twin[i].ssid);
        snprintf(r.line2, sizeof(r.line2), "%.17s  %s ago", s_twin[i].bssid, age);
      }
      break;
    case WdTile::TRACKERS:
      if (i < s_trkN) {
        wdAgeStr(s_trk[i].firstSeen, age, sizeof(age));
        snprintf(r.line1, sizeof(r.line1), "%s%.17s",
                 s_trk[i].watching ? "! " : "", s_trk[i].mac);
        snprintf(r.line2, sizeof(r.line2), "here %s  RSSI %d", age, (int)s_trk[i].rssi);
      }
      break;
    case WdTile::KARMA:
      if (i < s_karmaN) {
        wdAgeStr(s_karma[i].at, age, sizeof(age));
        snprintf(r.line1, sizeof(r.line1), "%.17s", s_karma[i].bssid);
        snprintf(r.line2, sizeof(r.line2), "answered \"%s\"  %s ago", s_karma[i].ssid, age);
      }
      break;
    case WdTile::CAMERAS:
      if (i < s_camN) {
        wdAgeStr(s_cam[i].at, age, sizeof(age));
        snprintf(r.line1, sizeof(r.line1), "%.12s hardware?", s_cam[i].vendor);
        snprintf(r.line2, sizeof(r.line2), "%.17s  %s ago", s_cam[i].bssid, age);
      }
      break;
    case WdTile::DRONES:
      if (i < s_droneN) {
        const RemoteId::Info& in = s_drone[i].info;
        if (in.haveBasic && in.serial[0])
          snprintf(r.line1, sizeof(r.line1), "Drone %.20s", in.serial);
        else
          snprintf(r.line1, sizeof(r.line1), "Drone %.17s", s_drone[i].mac);
        if (in.haveLoc)
          snprintf(r.line2, sizeof(r.line2), "%.5f, %.5f  alt %.0fm", in.lat, in.lon, in.altM);
        else
          snprintf(r.line2, sizeof(r.line2), "position decoding...");
      }
      break;
    default: break;
  }
  return r;
}

void detectorsClearTile(WdTile t) {
  // Dismiss a tile's findings (used after reviewing).
  switch (t) {
    case WdTile::EVILTWIN: {
      // "Clear" = I have looked at it and accept the current situation: forget
      // the remembered pairing for these SSIDs so the next beacon heard
      // re-baselines them (a replaced router otherwise alerts forever).
      for (int k = 0; k < s_twinN; k++) {
        for (int i = 0; i < s_tw.count; i++) {
          if (strncmp(s_tw.aps[i].ssid, s_twin[k].ssid, 32) == 0) {
            for (int j = i; j < s_tw.count - 1; j++) s_tw.aps[j] = s_tw.aps[j + 1];
            s_tw.count--;
            if (s_tw.next >= s_tw.count) s_tw.next = 0;
            break;
          }
        }
      }
      s_twinN = 0;
      break;
    }
    case WdTile::KARMA:    s_karmaN = 0; break;
    case WdTile::CAMERAS:  s_camN = 0; break;
    default: break;
  }
}
