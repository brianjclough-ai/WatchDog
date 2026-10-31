# WatchDog v1 — ESP32 CYD Airwave Watchdog

An always-on scanner for the ESP32 "Cheap Yellow Display" (2432S028R, ILI9341 320×240).
It passively watches the WiFi and Bluetooth airwaves, learns what's normal in your home
during a 24-hour learning period, then pops up an alert when something new or
suspicious shows up: rogue access points, evil twins, Bluetooth trackers, karma
responders, possible cameras, drones, or WiFi jamming.

**This is a standalone v1.** The plan is to merge it into the Bruce firmware fork later
(see `~/workspace/bruce-fork/ROADMAP.md`). Nothing here phones home — all processing
and storage is on the device.

## What v1 does

- **Dashboard** — one always-up screen: WatchDog header (ARMED/LEARNING + clock),
  3×3 tile grid (WiFi, BLE, JamDetect, EvilTwin, Trackers, Karma, Cameras, Drones,
  Known), firmware version in the footer.
- **Alert popup** — NEW DEVICE dialog over the dashboard with what/why/RSSI plus
  **Trust** (adds to baseline), **Watch** (snoozes 1 hour), **Details** (opens the
  tile's detail screen). Tapping outside the popup dismisses it.
- **Learning / Armed modes** — first boot learns for 24 h (everything seen becomes
  the baseline, no alerts). "Arm now" ends learning early. Learning expires
  automatically into ARMED.
- **Re-learn** — Settings → full re-learn (wipes baseline, confirm dialog:
  "This clears N known devices. Continue?") or quick additive re-learn (keeps the
  baseline, 4 h fold-in). Also via long-press on the Known tile (planned — v1 uses
  the Settings entries).
- **Merge-don't-alert fingerprinting** — devices are keyed by MAC, but a rotated
  MAC with the same BLE advert fingerprint + RSSI bucket is silently merged into
  the existing baseline entry. No duplicate popups for your own gear.
- **Per-tile detail screens** — tap any tile: scrollable list with age + RSSI.
- **Known devices** — baseline list with trusted checkmarks, radio type, Rename
  (on-screen keyboard) / Remove.
- **Sweep mode** (v1.1.0) — Settings → Sweep mode: a temporary high-sensitivity
  rental/Airbnb check. RAM-only session table; the home baseline on flash is
  never read for trust and never written. Orange SWEEP pill, room checklist
  (Living room, Kitchen, Bedroom 1/2, Bathroom + Add room), end-of-sweep
  findings summary, then discard-and-exit back to ARMED. See "Rental sweep"
  below.

## Rental sweep (v1.1.0)

Taking the WatchDog to a rental or Airbnb for a few days:

1. At home, make sure the unit is ARMED with your home baseline (normal state).
2. At the rental: gear icon → Settings → **Sweep mode**. The pill turns orange
   and reads SWEEP; the footer notes the session isn't saved.
3. Walk room to room with the unit. Tap each room in the checklist as you sweep
   it; check its box when done. Everything unknown alerts — cameras (OUI),
   persistent BLE tags, rogue APs, evil twins, drones.
4. **Trust** on a popup during a sweep only quiets it *for this sweep* — your
   home baseline is untouched.
5. **End sweep** → summary of findings by category → **Discard & exit**.
   Home baseline intact, back to ARMED.

Do NOT run a normal re-learn at a rental: learning trusts everything it sees,
which would whitelist any spying devices into your home baseline. Sweep mode
exists precisely so that can't happen.

Back it up with two phone checks the firmware can't do: join the rental WiFi
and run Fing (anything camera-like on the device list gets investigated), then
lights out and sweep the room with your phone camera — night-vision cameras
glow in the dark. And physically look for anything that doesn't transmit at
all: no scanner sees those.

## Detectors (per tile)

| Tile | What it implements in v1 |
|---|---|
| WiFi | AP sweep each cycle; new BSSID not in baseline → info alert. |
| BLE | Advert scan each cycle; new device → info alert; persistent unknown (5+ cycles, sensitivity-scaled) → escalated info alert. |
| Deauth (was JamDetect) | Promiscuous listen on ch 1/6/11 counts deauth/disassoc frames; over threshold (30/9 s window at Medium) → warning alert + tile ALERT until 2 quiet windows. |
| EvilTwin | Ported `twin_watch.h` engine, fed by promiscuous beacons: same SSID + different vendor + different encryption → EVIL TWIN warning; same encryption but different IE fingerprint → suspect warning. Carrier hotspots (xfinitywifi et al.) never judged. |
| Trackers | Apple FindMy pattern (`4C 00 12 19` + 25-byte chunk) keyed by chunk hash (survives MAC rotation); 3+ cycles (Medium) → warning alert. Simplified from donor `travel.h`: no GPS on this unit, persistence-only. |
| Karma | Ported `twin_watch.h` probe-response check: a BSSID answering for an SSID it was never seen beaconing → warning alert. Best-effort — only what the promiscuous window overhears. |
| Cameras | BSSID OUI lookup against a small starter table (Wyze ×2, Google/Nest). First sighting → warning alert. **The table is a starting point — verify and extend.** |
| Drones | `remote_id.cpp` (ported verbatim from the donor project): BLE legacy ASTM F3411 Remote ID detect + decode (serial, position, altitude when broadcast). First sighting → info alert. |
| Known | Baseline count; tap → device list. |

## Honest v1 limitations

- **Can't see what's not transmitting.** A camera recording to its own SD card, a wired
  camera, a passive bug — invisible to this and every airwave scanner.
- **Single radio, time-sliced.** ~15 s BLE → WiFi sweep → ~9 s promiscuous listen,
  then repeat. A ~30 s cycle means a brief visitor can slip between phases.
- **mDNS hostname anchoring is designed, not implemented.** The spec calls for mDNS
  as the primary fingerprint anchor, but a passive scanner that never joins a
  network can't observe mDNS. The baseline schema reserves the field; v1 relies on
  advert-payload fingerprint + RSSI bucket.
- **First-BSSID-wins for evil-twin.** Whichever AP is heard first becomes the
  baseline (inherited from the donor engine). If the rogue is already up at boot,
  the *real* AP trips the alert — the pair is still surfaced either way.
- **Karma detection is best-effort.** It only sees probe responses that happen to
  air during the 9 s listen window.
- **WiFi Remote ID (beacon form) not decoded** — BLE legacy form only. Bluetooth 5
  Long Range / Coded PHY is beyond the ESP32's BLE 4.2 radio (same limit the donor
  documents).
- **Camera OUI table is a starter list** (14 entries, unverified). Expect to grow it as you verify
  hardware.
- **Clock is uptime-based** (HH:MM since boot) — no RTC/NTP in v1.
- **Touch calibration** — the XPT2046 library returns raw 0-4095 values, so v1.0.1
  maps them to pixels in `ui_common.cpp` using `WD_TOUCH_X_MIN/X_MAX/Y_MIN/Y_MAX`
  (defaults 200/3700/240/3800). If taps land offset, tune those; if mirrored, add
  `-DWD_TOUCH_FLIP_X` and/or `-DWD_TOUCH_FLIP_Y` to `build_flags`. **Untested on
  hardware — verify first.**

## Build

You need [PlatformIO](https://platformio.org/) (`pio` on PATH).

```
cd ~/workspace/watchdog-firmware
pio run -e cyd-ili9341
```

First build downloads the toolchain + libraries (a few minutes). It must finish with
`[SUCCESS]` and zero errors.

## Flash

Brian's board is the **ILI9341** variant — always build/flash `[env:cyd-ili9341]`
(the ST7789 build white-screens this panel).

- **Web flasher / esptool:** `pio run -e cyd-ili9341 -t upload` with the CYD on USB
  (or flash `.pio/build/cyd-ili9341/firmware.bin` at 0x10000 with your usual tool —
  full `upload` also writes bootloader + partitions).
- After flashing: the unit boots into **LEARNING** (24 h). Leave it powered where it
  will live. It arms itself automatically, or tap **Arm now**.
- Flashing does not touch LittleFS on first install; the baseline persists across
  reboots and reflashes (unless you erase flash).

## Hardware

- ESP32 CYD 2432S028R (dedicated unit — keep Bruce on the other one)
- USB wall charger (always-on; no battery)
- No speaker on the CYD: alerts are screen-only in v1
- No GPS on this unit: no location-dependent features

## Project layout

```
platformio.ini            pinned espressif32@6.5.0, TFT_eSPI 2.5.43,
                          XPT2046_Touchscreen (git), NimBLE-Arduino 2.5.1,
                          ArduinoJson 7 (+ NimBLE observer-only trim flags)
include/
  cyd_ili9341_user_setup.h  working display config (copied from donor)
  twin_watch.h              evil-twin + karma engine (copied, header-only)
  remote_id.h               drone Remote ID decoder header (copied)
src/
  main.cpp          setup/loop, mode state machine, touch + draw dispatch
  wd_state.h        shared types + globals
  ui_common.*       display/touch init, theme, tap detection, buttons, headers
  ui_dashboard.*    3x3 tile grid (the approved mockup)
  ui_alert.*        NEW DEVICE popup: Trust / Watch / Details
  ui_detail.*       per-tile scrollable finding list
  ui_known.*        baseline list + rename keyboard + remove
  ui_learning.*     learning progress + Arm now; settings + re-learn + confirm
  scan.*            BLE (15 s) -> WiFi sweep -> promiscuous listen (9 s) loop
  baseline.*        LittleFS JSON store + composite fingerprinting
  detectors.*       one detector family per tile
  remote_id.cpp     drone decoder (copied from donor, Arduino-free)
  sweep.*           v1.1.0: sweep mode - room checklist + summary, session mgmt
```

## Warnings

v1.1.0 compiles with **zero errors and zero warnings** in `src/` (PlatformIO 6.2.0,
`pio run -e cyd-ili9341`, 2026-10-11).

## v1.1.0 / v1.0.2 / v1.0.1 changes

See `CHANGES.md`. v1.0.2 was rebuilt for the ESP32 after review on 2026-10-11
(`pio run -e cyd-ili9341` → **[SUCCESS]**, zero errors, RAM 27%, Flash 78.8%).
Review fixes verified in the build: camera OUI table checked against the IEEE
registry — 13/14 confirmed, A4:DA:22 ("Wyze") was mislabeled (registry says
"IEEE Registration Authority") and removed. Not yet run on CYD hardware —
touch mapping, BLE/WiFi coexistence, and alert chattiness still need the
Tuesday board test.
