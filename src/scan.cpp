// WatchDog v1 — scan implementation.
#include "scan.h"
#include "detectors.h"
#include "twin_watch.h"   // ieFingerprint for the promiscuous path
#include <NimBLEDevice.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ---- phase timing ----------------------------------------------------------
#define BLE_PHASE_MS    15000
#define LISTEN_PHASE_MS 9000
#define LISTEN_CH_MS    3000
static const uint8_t kListenCh[] = {1, 6, 11};

enum class ScanPhase : uint8_t { BLE, WIFI, LISTEN, PROCESS };

static ScanPhase  s_phase = ScanPhase::BLE;
static uint32_t   s_phaseStart = 0;
static uint8_t    s_listenIdx = 0;
static uint32_t   s_listenChStart = 0;
static volatile int s_deauthCount = 0;

// ---- BLE advert queue (filled from the NimBLE task, drained in loop) ------
#define BLEQ_N 64
struct BleQEnt {
  char    mac[18];
  int8_t  rssi;
  uint8_t payload[62];
  uint8_t len;
};
static BleQEnt s_bleq[BLEQ_N];
static volatile uint8_t s_bleqW = 0;
static uint8_t s_bleqR = 0;

class WdScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* adv) override {
    uint8_t w = s_bleqW;
    uint8_t n = (uint8_t)((w + 1) % BLEQ_N);
    if (n == s_bleqR) return;  // full: drop this one
    BleQEnt& e = s_bleq[w];
    std::string a = adv->getAddress().toString();
    strncpy(e.mac, a.c_str(), sizeof(e.mac) - 1);
    e.mac[sizeof(e.mac) - 1] = 0;
    e.rssi = adv->getRSSI();
    const std::vector<uint8_t>& pl = adv->getPayload();
    size_t nbytes = pl.size();
    e.len = nbytes > sizeof(e.payload) ? sizeof(e.payload) : (uint8_t)nbytes;
    memcpy(e.payload, pl.data(), e.len);
    __sync_synchronize();   // entry fully written before the index moves
    s_bleqW = n;
  }
};

static void drainBleQueue() {
  while (s_bleqR != s_bleqW) {
    __sync_synchronize();
    BleQEnt& e = s_bleq[s_bleqR];
    detectorsNoteBle(e.mac, e.rssi, e.payload, e.len);
    s_bleqR = (uint8_t)((s_bleqR + 1) % BLEQ_N);
  }
}

// ---- promiscuous ring (filled from the WiFi RX task, drained in loop) -----
#define PRING_N 64
struct PringEnt {
  uint8_t  subtype;   // 8 = beacon, 5 = probe response
  uint8_t  bssid[6];
  char     ssid[33];
  bool     enc;
  uint32_t ieHash;
};
static PringEnt s_pring[PRING_N];
static volatile uint8_t s_pringW = 0;
static uint8_t s_pringR = 0;

// (Runs in the Wi-Fi task, not an ISR, so it does not need IRAM_ATTR.)
static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = pkt->payload;
  uint16_t fc = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
  uint8_t subtype = (uint8_t)((fc >> 4) & 0x0F);

  if (subtype == 0x0C || subtype == 0x0A) {  // deauth / disassoc
    s_deauthCount++;
    return;
  }
  if (subtype != 0x08 && subtype != 0x05) return;  // beacon / probe resp only

  uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 38) return;
  // addr2 (transmitter = BSSID for both frame types) at offset 10.
  // Fixed params: 12 bytes at 24. IEs follow at 36.
  const uint8_t* ies = f + 36;
  uint16_t iesLen = len - 36;
  uint16_t caps = (uint16_t)f[34] | ((uint16_t)f[35] << 8);
  bool enc = (caps & 0x0010) != 0;

  char ssid[33]; ssid[0] = 0;
  uint16_t i = 0;
  while (i + 2 <= iesLen) {
    uint8_t id = ies[i], el = ies[i + 1];
    if (i + 2 + el > iesLen) break;
    if (id == 0 && el > 0 && el < 33) {
      memcpy(ssid, ies + i + 2, el);
      ssid[el] = 0;
    } else if (id == 48) {
      enc = true;  // RSN = WPA2/3
    } else if (id == 221 && el >= 4 && ies[i + 2] == 0x00 &&
               ies[i + 3] == 0x50 && ies[i + 4] == 0xF2 && ies[i + 5] == 0x01) {
      enc = true;  // WPA
    }
    i = (uint16_t)(i + 2 + el);
  }
  if (!ssid[0]) return;  // hidden: nothing to judge a twin on

  uint8_t w = s_pringW;
  uint8_t n = (uint8_t)((w + 1) % PRING_N);
  if (n == s_pringR) return;
  PringEnt& e = s_pring[w];
  e.subtype = subtype;
  memcpy(e.bssid, f + 10, 6);
  memcpy(e.ssid, ssid, sizeof(e.ssid));
  e.enc = enc;
  e.ieHash = ieFingerprint(ies, iesLen);
  __sync_synchronize();   // entry fully written before the index moves
  s_pringW = n;
}

static void drainPring() {
  while (s_pringR != s_pringW) {
    __sync_synchronize();
    PringEnt& e = s_pring[s_pringR];
    if (e.subtype == 0x08)
      detectorsNoteBeacon(e.bssid, e.ssid, e.enc, e.ieHash);
    else
      detectorsNoteProbeResp(e.bssid, e.ssid);
    s_pringR = (uint8_t)((s_pringR + 1) % PRING_N);
  }
}

// ---- phases -----------------------------------------------------------------

static NimBLEScan* s_pScan = nullptr;

static void enterBle() {
  s_phase = ScanPhase::BLE;
  s_phaseStart = millis();
  esp_wifi_set_promiscuous(false);
  s_pScan->clearResults();
  s_pScan->start(0, false);  // indefinite; we stop it on phase exit
}

static void enterWifi() {
  s_phase = ScanPhase::WIFI;
  s_phaseStart = millis();
  if (s_pScan->isScanning()) s_pScan->stop();
  drainBleQueue();
}

static void enterListen() {
  s_phase = ScanPhase::LISTEN;
  s_phaseStart = millis();
  s_listenIdx = 0;
  s_listenChStart = millis();
  s_deauthCount = 0;
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(kListenCh[0], WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
}

static void enterProcess() {
  s_phase = ScanPhase::PROCESS;
  s_phaseStart = millis();
  esp_wifi_set_promiscuous(false);
  drainPring();
  detectorsNoteDeauthWindow(s_deauthCount);
  detectorsCycleDone();
}

void scanInit() {
  NimBLEDevice::init("");
  s_pScan = NimBLEDevice::getScan();
  s_pScan->setScanCallbacks(new WdScanCb(), false);
  s_pScan->setActiveScan(true);
  s_pScan->setInterval(100);
  s_pScan->setWindow(80);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  esp_wifi_set_promiscuous_rx_cb(&promiscCb);
  enterBle();
}

void scanTick() {
  drainBleQueue();  // keep the queue moving in every phase
  uint32_t now = millis();

  switch (s_phase) {
    case ScanPhase::BLE:
      if (now - s_phaseStart >= BLE_PHASE_MS) enterWifi();
      break;

    case ScanPhase::WIFI: {
      // Blocking sweep (~2-6 s). Acceptable: the dashboard simply
      // holds its last frame while this runs.
      int n = WiFi.scanNetworks(false, true, false, 300);
      for (int i = 0; i < n; i++) {
        String ss = WiFi.SSID(i);
        uint8_t* b = WiFi.BSSID(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        detectorsNoteAp(ss.c_str(), b, (int8_t)WiFi.RSSI(i),
                        (uint8_t)WiFi.channel(i),
                        auth != WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();
      enterListen();
      break;
    }

    case ScanPhase::LISTEN:
      drainPring();   // keep the ring moving DURING the window so it never fills and drops beacons
      if (now - s_listenChStart >= LISTEN_CH_MS) {
        s_listenIdx++;
        if (s_listenIdx >= sizeof(kListenCh)) { enterProcess(); break; }
        s_listenChStart = now;
        esp_wifi_set_channel(kListenCh[s_listenIdx], WIFI_SECOND_CHAN_NONE);
      }
      break;

    case ScanPhase::PROCESS:
      enterBle();
      break;
  }
}
