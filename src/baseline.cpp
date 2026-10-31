// WatchDog v1 — baseline implementation.
//
// v1.0.1: crash-safe persistence. Saves go to a temp file, are read back and
// verified, and only then rotated into place (baseline.json -> baseline.bak,
// baseline.tmp -> baseline.json). Load tries json, then tmp, then bak, so a
// power cut at any point leaves a recoverable copy. Sightings no longer mark
// the file dirty (only real changes do), which stops the constant rewrites.
#include "baseline.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

#define WD_BASELINE_PATH "/wd/baseline.json"
#define WD_BASELINE_TMP  "/wd/baseline.tmp"
#define WD_BASELINE_BAK  "/wd/baseline.bak"
#define WD_BASELINE_PRE  "/wd/prelearn.json"

static WdDevice s_devs[WD_BASELINE_MAX];
static int      s_count = 0;
// Sweep session table: RAM-only, discarded on sweep exit. While a sweep is
// active every baseline*() call below transparently operates on this table
// instead of the LittleFS-backed home baseline, so detectors keep working
// unchanged and the home baseline is never read for trust nor written.
#define WD_SESSION_MAX 64
static WdDevice s_sdevs[WD_SESSION_MAX];
static int      s_scount = 0;
static bool     s_session = false;

static WdDevice* devArr()  { return s_session ? s_sdevs : s_devs; }
static int       devCap()  { return s_session ? WD_SESSION_MAX : WD_BASELINE_MAX; }
static int&      devCountRef() { return s_session ? s_scount : s_count; }
static int       devCount()    { return s_session ? s_scount : s_count; }

void baselineSessionBegin() {
  s_scount = 0;
  s_session = true;
}
void baselineSessionEnd() {
  s_session = false;   // home table untouched the whole time
  s_scount = 0;        // discard the session
}
bool baselineInSession() { return s_session; }
static bool     s_dirty = false;
static uint32_t s_lastSave = 0;
static bool     s_fsOk = false;
static bool     s_saveOk = true;
static int      s_overflow = 0;

uint32_t fpHashBle(const uint8_t* payload, uint8_t len) {
  // Walk AD structures; hash only the stable identifiers.
  uint32_t h = 2166136261u;
  uint8_t i = 0;
  while (i < len) {
    uint8_t elen = payload[i];
    if (elen == 0 || i + elen >= len) break;
    uint8_t type = payload[i + 1];
    // 0xFF mfg specific, 0x16 service data 16-bit, 0x03/0x07 service UUIDs,
    // 0x01 flags, 0x0A tx power. Skip 0x08/0x09 (name) and the rest.
    if (type == 0xFF || type == 0x16 || type == 0x03 || type == 0x07 ||
        type == 0x01 || type == 0x0A) {
      h ^= type; h *= 16777619u;
      for (uint8_t k = 0; k < elen; k++) { h ^= payload[i + 1 + k]; h *= 16777619u; }
    }
    i += (uint8_t)(elen + 1);
  }
  return h;
}

// Parse one baseline file into s_devs. Returns false (and leaves s_count
// untouched) if the file is missing, unreadable or not a valid baseline.
static bool loadFrom(const char* path) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  JsonArray arr = doc["devices"].as<JsonArray>();
  if (arr.isNull()) return false;
  s_count = 0;
  for (JsonObject o : arr) {
    if (s_count >= WD_BASELINE_MAX) break;
    WdDevice& d = s_devs[s_count++];
    strlcpy(d.label, o["label"] | "?", sizeof(d.label));
    strlcpy(d.mac, o["mac"] | "", sizeof(d.mac));
    d.radio      = o["radio"] | 0;
    d.fpHash     = o["fp"] | 0;
    d.rssiBucket = o["rssi"] | 0;
    d.firstSeen  = 0;   // seconds-since-boot values are meaningless after a reboot
    d.lastSeen   = 0;
    d.trusted    = o["trusted"] | true;
  }
  return true;
}

// Number of devices a file holds, or -1 if it does not parse.
static int countIn(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return -1;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return -1;
  JsonArray arr = doc["devices"].as<JsonArray>();
  if (arr.isNull()) return -1;
  return (int)arr.size();
}

void baselineInit() {
  s_count = 0;
  s_overflow = 0;
  s_fsOk = LittleFS.begin(true);   // formats only if the mount fails (first boot / dead FS)
  if (!s_fsOk) return;
  LittleFS.mkdir("/wd");
  if (loadFrom(WD_BASELINE_PATH)) return;
  // Main file missing or corrupt: fall back, then rewrite a clean main file.
  if (loadFrom(WD_BASELINE_TMP) || loadFrom(WD_BASELINE_BAK)) s_dirty = true;
}

void baselineSave() {
  if (s_session) return;  // session table is RAM-only: never persisted
  s_dirty = true;  // flushed lazily from the main loop (LittleFS writes are slow)
}

static bool baselineFlush() {
  if (!s_fsOk) return false;
  File f = LittleFS.open(WD_BASELINE_TMP, "w");
  if (!f) return false;
  f.print("{\"devices\":[");
  for (int i = 0; i < s_count; i++) {
    if (i) f.print(',');
    JsonDocument o;
    o["label"]   = s_devs[i].label;
    o["mac"]     = s_devs[i].mac;
    o["radio"]   = s_devs[i].radio;
    o["fp"]      = s_devs[i].fpHash;
    o["rssi"]    = s_devs[i].rssiBucket;
    o["trusted"] = s_devs[i].trusted;
    serializeJson(o, f);
  }
  f.print("]}");
  bool wrote = (f.getWriteError() == 0);
  f.close();
  // Read it back: only a file that parses with the right count replaces the good one.
  if (!wrote || countIn(WD_BASELINE_TMP) != s_count) {
    LittleFS.remove(WD_BASELINE_TMP);
    return false;
  }
  LittleFS.remove(WD_BASELINE_BAK);
  if (LittleFS.exists(WD_BASELINE_PATH)) LittleFS.rename(WD_BASELINE_PATH, WD_BASELINE_BAK);
  if (!LittleFS.rename(WD_BASELINE_TMP, WD_BASELINE_PATH)) {
    LittleFS.rename(WD_BASELINE_BAK, WD_BASELINE_PATH);  // put the old one back
    return false;
  }
  return true;
}

// Call from the main loop: rate-limits LittleFS writes.
void baselineTick() {
  if (s_session) return;  // sweep sessions never touch flash
  uint32_t now = millis();
  if (s_dirty && now - s_lastSave > 5000) {
    s_lastSave = now;
    s_dirty = false;
    s_saveOk = baselineFlush();
    if (!s_saveOk) s_dirty = true;   // keep trying
  }
}

bool baselineStorageOk() { return s_fsOk && s_saveOk; }
int  baselineOverflowCount() { return s_overflow; }

void baselineBackupForRelearn() {
  if (!s_fsOk) return;
  if (s_dirty) { s_dirty = false; s_saveOk = baselineFlush(); if (!s_saveOk) s_dirty = true; }
  if (!LittleFS.exists(WD_BASELINE_PATH)) return;
  File in = LittleFS.open(WD_BASELINE_PATH, "r");
  if (!in) return;
  File out = LittleFS.open(WD_BASELINE_PRE, "w");
  if (!out) { in.close(); return; }
  uint8_t buf[256];
  int n;
  while ((n = in.read(buf, sizeof(buf))) > 0) out.write(buf, (size_t)n);
  out.close();
  in.close();
}

int baselineCount() { return devCount(); }

WdDevice* baselineAt(int idx) {
  if (idx < 0 || idx >= devCount()) return nullptr;
  return &devArr()[idx];
}

int baselineIndexOf(const WdDevice* d) {
  if (!d) return -1;
  ptrdiff_t i = d - devArr();
  if (i < 0 || i >= devCount()) return -1;
  return (int)i;
}

WdDevice* baselineFindByMac(const char* mac, uint8_t radio) {
  if (!mac) return nullptr;
  WdDevice* arr = devArr();
  for (int i = 0; i < devCount(); i++) {
    if (arr[i].radio == radio && strcasecmp(arr[i].mac, mac) == 0)
      return &arr[i];
  }
  return nullptr;
}

int baselineMatchFingerprint(uint32_t fpHash, int8_t rssiBucket, uint8_t radio) {
  if (!fpHash) return -1;
  WdDevice* arr = devArr();
  for (int i = 0; i < devCount(); i++) {
    if (arr[i].radio != radio) continue;
    if (arr[i].fpHash != fpHash) continue;
    // A rotated MAC means the OLD one went quiet. If that entry was heard in
    // the last few seconds, this is a second, look-alike device, not a
    // rotation: do not merge it silently.
    if (arr[i].lastSeen != 0 && wdNow() - arr[i].lastSeen < 10) continue;
    int8_t d = arr[i].rssiBucket - rssiBucket;
    if (d < 0) d = -d;
    if (d <= 1) return i;  // same device, rotated MAC
  }
  return -1;
}

int baselineAdd(const char* mac, uint8_t radio, uint32_t fpHash,
                int8_t rssi, const char* label) {
  if (!mac) return -1;
  WdDevice* existing = baselineFindByMac(mac, radio);
  if (existing) return baselineIndexOf(existing);   // never duplicate a MAC
  if (devCount() >= devCap()) { s_overflow++; return -1; }
  WdDevice* arr = devArr();
  int idx = devCountRef()++;
  WdDevice& d = arr[idx];
  strlcpy(d.label, label && label[0] ? label : "Unknown", sizeof(d.label));
  strlcpy(d.mac, mac, sizeof(d.mac));
  d.radio      = radio;
  d.fpHash     = fpHash;
  d.rssiBucket = rssiBucketOf(rssi);
  d.firstSeen  = wdNow();
  d.lastSeen   = wdNow();
  d.trusted    = true;
  if (!s_session) s_dirty = true;
  return idx;
}

void baselineTouch(int idx, int8_t rssi) {
  if (idx < 0 || idx >= devCount()) return;
  devArr()[idx].lastSeen = wdNow();
  devArr()[idx].rssiBucket = rssiBucketOf(rssi);
  // Deliberately NOT marking dirty: sightings are runtime state, and
  // rewriting flash on every advert wore the file system and widened the
  // power-loss window.
}

void baselineRemove(int idx) {
  if (idx < 0 || idx >= devCount()) return;
  WdDevice* arr = devArr();
  int n = devCount();
  for (int i = idx; i < n - 1; i++) arr[i] = arr[i + 1];
  devCountRef()--;
  if (!s_session) s_dirty = true;
}

void baselineRename(int idx, const char* label) {
  if (idx < 0 || idx >= devCount() || !label) return;
  strlcpy(devArr()[idx].label, label, sizeof(devArr()[idx].label));
  if (!s_session) s_dirty = true;
}

void baselineClear() {
  devCountRef() = 0;
  s_overflow = 0;
  if (!s_session) s_dirty = true;
}
