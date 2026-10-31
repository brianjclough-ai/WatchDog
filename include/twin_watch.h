// SquachWatch-CYD — evil-twin evidence, in two parts.
//
// PART 1: the beacon table. The first BSSID seen beaconing an SSID becomes
// the baseline, with the security posture it advertised. A later beacon
// for that SSID from different hardware is judged in escalating order:
//
//   same BSSID            -> just the AP again; re-baseline, never a twin.
//   same vendor (OUI)     -> mesh node or the box's other band; never a twin.
//   different vendor,     -> the classic evil twin: an open clone of an
//     encryption differs      encrypted network (or the reverse). HIGH.
//   different vendor,     -> the encryption-matching attacker the old code
//     encryption agrees,     let walk past. Same SSID, same security, but a
//     IE shape differs        different capability fingerprint -- different
//                             hardware claiming to be the same network.
//                             SUSPICIOUS, not certain: Medium.
//
// The fingerprint is FNV-1a over the beacon's IE tag/length pairs, not
// values: values carry per-beacon traffic (TIM) and per-unit noise, while
// the tag/length sequence is the AP's stable capability shape. Two radios
// of the same model converge; different vendors' firmware never does.
//
// PART 2: Karma. A probe RESPONSE from a BSSID for an SSID it was never
// seen beaconing is a Karma/Mana responder answering somebody else's
// probe -- the "connect to any network you ask for" attack. Only positive
// evidence counts: the BSSID must be on file beaconing a DIFFERENT SSID.
// A BSSID never seen at all stays quiet, because the table only knows what
// it has heard since boot and an unheard beacon is not a lie.
//
// One inherent limitation, kept from the original: whichever BSSID is seen
// first becomes the baseline. If the rogue is already up when you arrive,
// it gets recorded as legitimate and the real AP trips the alert. The pair
// is still surfaced either way.
//
// Header-only and free of Arduino, so the host tests drive it with their
// own frames. The engine owns one and feeds it from the WiFi queue.
#pragma once
#include <stdint.h>
#include <string.h>

// Carrier hotspot SSIDs, broadcast by design from thousands of gateways of
// many vendors (every Comcast box in the neighborhood beacons xfinitywifi).
// Judging them as twins is pure noise, so they never produce a verdict and
// never take a baseline slot. Case-insensitive. Extend freely: one SSID per
// line, keep the list sorted-ish by carrier for readability.
static const char* const kTwinHotspotSsids[] = {
    "xfinitywifi", "XFINITY",
    "SpectrumWiFi", "SpectrumMobile",
    "attwifi",
    "CoxWiFi", "CableWiFi",
    "ShawOpen", "ShawMobile",
    "TWCWiFi", "optimumwifi", "FrontierWiFi",
};

static inline bool twinIsHotspotSsid(const char* ssid) {
    if (!ssid || !ssid[0]) return false;
    for (uint8_t i = 0;
         i < sizeof(kTwinHotspotSsids) / sizeof(kTwinHotspotSsids[0]); i++) {
        const char* h = kTwinHotspotSsids[i];
        for (uint8_t j = 0; ; j++) {
            char a = h[j], b = ssid[j];
            if (a >= 'A' && a <= 'Z') a += (char)32;
            if (b >= 'A' && b <= 'Z') b += (char)32;
            if (a != b) break;
            if (a == 0) return true;
        }
    }
    return false;
}

// Same vendor, ignoring the locally-administered bit. Multi-SSID and mesh
// APs routinely derive per-radio BSSIDs by setting that bit on their base
// MAC (34:12:98 becomes 36:12:98) -- byte 0 differs by 2 and a plain
// memcmp calls that a different manufacturer, which is how the first
// version of this managed to flag an entire mesh network.
static inline bool twinSameVendor(const uint8_t* a, const uint8_t* b) {
    return ((a[0] & ~0x02) == (b[0] & ~0x02)) && a[1] == b[1] && a[2] == b[2];
}

// The beacon's capability fingerprint: FNV-1a over (tag, length) pairs.
// Tag 0 (SSID) is the table's lookup key, not evidence, so it is skipped;
// values are skipped because TIM, QBSS load and vendor blobs change
// beacon to beacon on the same box. `ies` starts at the first element,
// `len` is the element region's length (no FCS).
static inline uint32_t ieFingerprint(const uint8_t* ies, uint16_t len) {
    uint32_t h = 2166136261u;
    uint16_t i = 0;
    while ((uint32_t)i + 2u <= len) {
        const uint8_t id = ies[i], el = ies[i + 1];
        if ((uint32_t)i + 2u + el > len) break;   // runs off the frame: stop
        if (id != 0) {
            h ^= id; h *= 16777619u;
            h ^= el; h *= 16777619u;
        }
        i = (uint16_t)(i + 2 + el);
    }
    return h;
}

struct TwinWatch {
    static const uint8_t AP_CAP = 24;

    struct ApEntry {
        char     ssid[33];
        uint8_t  bssid[6];
        uint8_t  encrypted;
        uint32_t ieHash;
        // The baseline BSSID's first-seen beacon TSF (µs of AP uptime).
        // A twin's uptime almost never matches the real AP's, so the
        // baseline carries it as a hint -- not proof, and never a
        // verdict on its own: some APs randomize the TSF.
        uint64_t tsfFirst;
    };
    ApEntry aps[AP_CAP];
    uint8_t count = 0;
    uint8_t next  = 0;   // round-robin eviction cursor

    enum class Verdict : uint8_t {
        NONE,        // no twin: baseline, mesh, or first sighting
        EVIL_TWIN,   // different vendor AND encryption disagreement
        SUSPECT_TWIN // different vendor, encryption agrees, IE shape differs
    };

    // One beacon. Records the SSID on first sighting; otherwise returns
    // the verdict for this BSSID against the baseline. `tsf` is the
    // beacon's TSF (0 when the frame was too short to carry one); it is
    // recorded on the baseline entry as the uptime hint, never as a
    // verdict input.
    Verdict noteBeacon(const uint8_t* bssid, const char* ssid,
                       bool encrypted, uint32_t ieHash, uint64_t tsf = 0) {
        if (!bssid || !ssid) return Verdict::NONE;
        // Carrier hotspots (xfinitywifi et al.) are the same SSID from
        // thousands of legitimately different vendors' gateways: they can
        // never be judged as twins, and they take no baseline slot. The
        // beacon still flows through the engine's normal OUI/SSID lookup.
        if (twinIsHotspotSsid(ssid)) return Verdict::NONE;
        for (uint8_t i = 0; i < count; i++) {
            if (strncmp(aps[i].ssid, ssid, 32) != 0) continue;
            if (memcmp(aps[i].bssid, bssid, 6) == 0) {
                // Same box beaconing again: re-baseline, so a legitimate
                // reconfiguration stops alerting instead of alerting
                // forever after.
                aps[i].encrypted = encrypted ? 1 : 0;
                aps[i].ieHash    = ieHash;
                if (tsf && !aps[i].tsfFirst) aps[i].tsfFirst = tsf;
                return Verdict::NONE;
            }
            if (twinSameVendor(aps[i].bssid, bssid)) return Verdict::NONE;
            if (aps[i].encrypted != (encrypted ? 1 : 0)) return Verdict::EVIL_TWIN;
            // Same encryption from different hardware: the fingerprint is
            // the tiebreaker. Both zero means neither side had elements to
            // hash (a runt frame), which is no evidence at all.
            if (ieHash && aps[i].ieHash && ieHash != aps[i].ieHash)
                return Verdict::SUSPECT_TWIN;
            return Verdict::NONE;
        }
        // First sighting of this SSID: record it as the baseline.
        // Round-robin eviction once full, so a busy area can't grow this
        // without bound -- the same bound the BLE path learned the hard
        // way.
        ApEntry& slot = (count < AP_CAP) ? aps[count++] : aps[next];
        if (count >= AP_CAP) next = (uint8_t)((next + 1) % AP_CAP);
        strncpy(slot.ssid, ssid, 32);
        slot.ssid[32] = 0;
        memcpy(slot.bssid, bssid, 6);
        slot.encrypted = encrypted ? 1 : 0;
        slot.ieHash    = ieHash;
        slot.tsfFirst  = tsf;
        return Verdict::NONE;
    }

    // One probe response: BSSID `bssid` answering for `ssid`. True when
    // that BSSID is on file but never for THIS SSID -- a Karma-style
    // responder. A BSSID never seen beaconing is not evidence (the table
    // only knows what it has heard since boot), and an answer for an SSID
    // it does beacon is a directed probe being answered normally. Every
    // entry for the BSSID is checked, so a box beaconing two SSIDs from
    // one BSSID is not misread.
    bool noteProbeResp(const uint8_t* bssid, const char* ssid) {
        if (!bssid || !ssid || !ssid[0]) return false;
        // Same hotspot rule as noteBeacon: a carrier hotspot SSID is never
        // Karma evidence either.
        if (twinIsHotspotSsid(ssid)) return false;
        bool seen = false;
        for (uint8_t i = 0; i < count; i++) {
            if (memcmp(aps[i].bssid, bssid, 6) != 0) continue;
            seen = true;
            if (strncmp(aps[i].ssid, ssid, 32) == 0) return false;
        }
        return seen;
    }
};
