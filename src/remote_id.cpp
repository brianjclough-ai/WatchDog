// SquachWatch-CYD — ASTM F3411 Remote ID decoder. See remote_id.h.
#include "remote_id.h"
#include <string.h>

namespace RemoteId {

// ---- the wire format ----------------------------------------------------
// A Bluetooth Legacy advert is a run of AD structures, each one a length
// byte, a type byte, then length-1 bytes of data. Remote ID rides in the
// Service Data structure (type 0x16) under UUID 0xFFFA, and inside that
// sits a one-byte application code, a one-byte message counter, then
// exactly 25 bytes of message.
static const uint8_t AD_SERVICE_DATA_16 = 0x16;
static const uint16_t ODID_UUID         = 0xFFFA;
static const uint8_t ODID_APP_CODE      = 0x0D;
static const uint8_t MSG_SIZE           = 25;

// Message types live in the TOP nibble of byte 0. The bottom nibble is the
// protocol version, which we do not check: the field layouts this reads
// have been stable across every published version, and refusing to decode
// a drone because it advertised version 3 would be worse than decoding it.
static const uint8_t MSG_BASIC_ID = 0x0;
static const uint8_t MSG_LOCATION = 0x1;
static const uint8_t MSG_SYSTEM   = 0x4;

// Fixed-point scales, straight out of the reference implementation:
// coordinates are degrees x 10^7, and altitude is half-metres offset by
// 1000m so that below-sea-level values still fit an unsigned 16-bit field.
static const double LATLON_MULT = 10000000.0;
static const float  ALT_DIV     = 0.5f;
static const float  ALT_ADDER   = 1000.0f;

// Little-endian, because the standard's structs are packed and every
// device implementing it is little-endian. Read byte by byte rather than
// cast: the payload comes off the radio unaligned, and a 32-bit load at an
// odd address is a LoadStoreError exception on this chip rather than a
// slow read like it would be on a PC.
static int32_t rd32(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

const char* uaTypeName(uint8_t t) {
    switch (t) {
        case 1:  return "PLANE";
        case 2:  return "QUAD";
        case 3:  return "HELI";
        case 4:  return "GYRO";
        case 5:  return "VTOL";
        case 6:  return "ORNITH";
        case 7:  return "GLIDER";
        case 8:  return "KITE";
        case 9:  return "FREEBAL";
        case 10: return "CAPBAL";
        case 11: return "AIRSHIP";
        case 12: return "PARA";
        case 13: return "ROCKET";
        case 14: return "TETHER";
        case 15: return "GROUND";
        default: return "UNKNOWN";
    }
}

void reset(Info& out) { out = Info(); }

// Decodes one 25-byte message body into `out`.
static bool mergeMessage(const uint8_t* m, Info& out, uint32_t now) {
    const uint8_t type = (uint8_t)((m[0] >> 4) & 0x0F);

    if (type == MSG_BASIC_ID) {
        // Byte 1 splits IDType (high) from UAType (low); the serial is the
        // next 20 bytes, space- or NUL-padded rather than terminated.
        out.uaType = (uint8_t)(m[1] & 0x0F);
        memcpy(out.serial, m + 2, 20);
        out.serial[20] = '\0';
        for (int i = 19; i >= 0; i--) {
            if (out.serial[i] == ' ' || out.serial[i] == '\0') out.serial[i] = '\0';
            else break;
        }
        out.haveBasic = true;
        out.at = now;
        return true;
    }

    if (type == MSG_LOCATION) {
        const int32_t la = rd32(m + 5);
        const int32_t lo = rd32(m + 9);
        // 0,0 is the standard's "unknown", and it is also a real place, so
        // treating it as no-fix is the lesser wrong of the two.
        if (la == 0 && lo == 0) return true;
        out.lat  = (float)((double)la / LATLON_MULT);
        out.lon  = (float)((double)lo / LATLON_MULT);
        out.altM = (float)rd16(m + 15) * ALT_DIV - ALT_ADDER;
        out.haveLoc = true;
        out.at = now;
        return true;
    }

    if (type == MSG_SYSTEM) {
        // The interesting one. Bytes 2-9 are where the pilot is standing.
        const int32_t la = rd32(m + 2);
        const int32_t lo = rd32(m + 6);
        if (la == 0 && lo == 0) return true;
        out.opLat = (float)((double)la / LATLON_MULT);
        out.opLon = (float)((double)lo / LATLON_MULT);
        out.haveOperator = true;
        out.at = now;
        return true;
    }

    // Auth, Self-ID, Operator-ID and Message Pack all reach here. Pack
    // (0xF) is the one worth naming: it wraps several messages at once and
    // is what the WiFi Beacon form uses, but it does not fit in a 31-byte
    // legacy advert, so nothing that arrives on this path can be one.
    return false;
}

// The Remote ID service data in a legacy advert: the pointer just past the
// AD type (UUID, app code, counter, message) and its size, or null.
static const uint8_t* findServiceData(const uint8_t* payload, uint8_t len, uint8_t& size) {
    if (!payload || len < 4) return nullptr;
    uint8_t i = 0;
    while (i < len) {
        const uint8_t adLen = payload[i];
        if (adLen == 0) break;
        if ((uint16_t)i + 1u + adLen > (uint16_t)len) break;
        const uint8_t  adType = payload[i + 1];
        const uint8_t* adData = payload + i + 2;
        const uint8_t  adSize = (uint8_t)(adLen - 1);
        if (adType == AD_SERVICE_DATA_16 && adSize >= 3 &&
            rd16(adData) == ODID_UUID && adData[2] == ODID_APP_CODE) {
            size = adSize;
            return adData;
        }
        i = (uint8_t)(i + 1 + adLen);
    }
    return nullptr;
}

bool present(const uint8_t* payload, uint8_t len) {
    uint8_t size = 0;
    return findServiceData(payload, len, size) != nullptr;
}

// ---- the WiFi Beacon form -------------------------------------------------
// A vendor-specific element (ID 221) under the ASD-STAN OUI FA:0B:BC, OUI
// type 0x0D, then a counter and a Message Pack: a header byte (type 0xF in
// the top nibble), the size of one message (25), how many follow (1-9),
// then the messages themselves back to back.
static const uint8_t IE_VENDOR = 221;
static const uint8_t ASD_OUI[3] = { 0xFA, 0x0B, 0xBC };
static const uint8_t MSG_PACK = 0xF;
static const uint8_t PACK_MAX = 9;

bool mergeBeacon(const uint8_t* ies, uint16_t len, Info& out, uint32_t now) {
    if (!ies) return false;
    uint16_t i = 0;
    while ((uint32_t)i + 2u <= len) {
        const uint8_t id = ies[i], ieLen = ies[i + 1];
        const uint8_t* body = ies + i + 2;
        if ((uint32_t)i + 2u + ieLen > len) break;       // runs off the frame: stop
        // OUI (3), OUI type, counter, pack header, message size, count.
        if (id == IE_VENDOR && ieLen >= 8 && memcmp(body, ASD_OUI, 3) == 0 && body[3] == ODID_APP_CODE) {
            const uint8_t* pack = body + 5;
            const uint8_t  room = (uint8_t)(ieLen - 5);
            if (((pack[0] >> 4) & 0x0F) != MSG_PACK || pack[1] != MSG_SIZE) return false;
            uint8_t n = pack[2];
            if (n > PACK_MAX) n = PACK_MAX;
            // Only the messages the element really holds.
            if (3u + (uint16_t)n * MSG_SIZE > room) n = (uint8_t)((room - 3) / MSG_SIZE);
            for (uint8_t k = 0; k < n; k++) mergeMessage(pack + 3 + k * MSG_SIZE, out, now);
            return true;
        }
        i = (uint16_t)(i + 2 + ieLen);
    }
    return false;
}

bool merge(const uint8_t* payload, uint8_t len, Info& out, uint32_t now) {
    // UUID, app code, counter, then the message itself.
    uint8_t size = 0;
    const uint8_t* d = findServiceData(payload, len, size);
    if (!d || size < 4 + MSG_SIZE) return false;
    return mergeMessage(d + 4, out, now);
}

// ---- DJI DroneID ----------------------------------------------------------
// Same vendor element, different OUI: 26:37:12, then three unknown bytes,
// a subcommand, and the payload. Only 0x10 (flight telemetry) is decoded;
// 0x11 is the operator's free-text flight purpose.
//
// Payload offsets below are from the first payload byte (seven past the IE
// body start). Every multi-byte field is little-endian. Anything that
// fails its sanity check is skipped field-by-field, so one moved offset in
// a new firmware costs that field, not the whole drone.
static const uint8_t DJI_OUI[3]   = { 0x26, 0x37, 0x12 };
static const uint8_t DJI_SUB_TELEM = 0x10;

// radians x 1e7 -> degrees: raw / 1e7 * (180 / pi)
static const double DJI_RAD_MULT = 174532.92519943295;  // 1e7 * pi / 180

static bool djiPrintable(const uint8_t* p, uint8_t n) {
    uint8_t good = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (p[i] == 0) continue;                       // null padding
        if (p[i] < 0x20 || p[i] > 0x7E) return false;
        good++;
    }
    return good >= 4;  // a serial shorter than this is not a serial
}

static bool djiCoordsOk(double lat, double lon) {
    if (lat == 0 && lon == 0) return false;            // the standard's "unknown"
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

bool mergeDji(const uint8_t* ies, uint16_t len, Info& out, uint32_t now) {
    if (!ies) return false;
    uint16_t i = 0;
    while ((uint32_t)i + 2u <= len) {
        const uint8_t id = ies[i], ieLen = ies[i + 1];
        const uint8_t* body = ies + i + 2;
        if ((uint32_t)i + 2u + ieLen > len) break;       // runs off the frame: stop
        // OUI (3), three unknown bytes, subcommand, then the payload.
        if (id == IE_VENDOR && ieLen >= 8 && memcmp(body, DJI_OUI, 3) == 0 &&
            body[6] == DJI_SUB_TELEM) {
            const uint8_t* p = body + 7;
            const uint16_t room = (uint16_t)(ieLen - 7);
            bool any = false;
            // Serial: 16 ASCII bytes at payload+5, null-padded.
            if (room >= 5 + 16 && djiPrintable(p + 5, 16)) {
                memcpy(out.serial, p + 5, 16);
                out.serial[16] = '\0';
                for (int k = 15; k >= 0; k--) {
                    if (out.serial[k] == '\0' || out.serial[k] == ' ') out.serial[k] = '\0';
                    else break;
                }
                out.haveBasic = true;
                any = true;
            }
            // Aircraft position: lon at +21, lat at +25, radians x 1e7.
            if (room >= 29) {
                const double lon = (double)rd32(p + 21) / DJI_RAD_MULT;
                const double lat = (double)rd32(p + 25) / DJI_RAD_MULT;
                if (djiCoordsOk(lat, lon)) {
                    out.lon = (float)lon;
                    out.lat = (float)lat;
                    out.haveLoc = true;
                    any = true;
                }
            }
            // Operator position: lat at +45, lon at +49. Note the order
            // flips relative to the aircraft fields above -- that is the
            // documented layout, not a typo.
            if (room >= 53) {
                const double lat = (double)rd32(p + 45) / DJI_RAD_MULT;
                const double lon = (double)rd32(p + 49) / DJI_RAD_MULT;
                if (djiCoordsOk(lat, lon)) {
                    out.opLat = (float)lat;
                    out.opLon = (float)lon;
                    out.haveOperator = true;
                    any = true;
                }
            }
            if (any) out.at = now;
            return any;
        }
        i = (uint16_t)(i + 2 + ieLen);
    }
    return false;
}

}  // namespace RemoteId
