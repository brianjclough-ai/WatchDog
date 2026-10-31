# WatchDog v1.1.0 changes

Sweep mode for rental/Airbnb checks: a temporary high-sensitivity session
that never touches the home baseline.

## Added
- **Sweep mode** (Settings → Sweep mode, ARMED only): orange SWEEP pill on the
  dashboard, footer notes "session only".
- **RAM-only session table**: while sweeping, every baseline call operates on
  a 64-entry throwaway table. `/wd/baseline.json`, `/wd/twins.bin` and
  `/wd/alerts.json` are never read for trust decisions and never written.
  Detectors needed no changes — they keep calling the same baseline API.
- **Trust (sweep only)**: the popup's Trust button is labeled "(sweep only)"
  during a sweep; trusting stops re-alerts for this sweep without touching
  the home baseline.
- **Forced HIGH sensitivity** during sweeps regardless of the user's setting
  (tracker 2 cycles, persistence 3, deauth 15 frames); restored on exit.
- **Twin engine parked**: home SSID→AP pairings are snapshotted aside at sweep
  start (a rental must not pollute them) and restored byte-identical on exit.
- **Room checklist**: Living room, Kitchen, Bedroom 1, Bedroom 2, Bathroom
  plus Add room (on-screen keyboard, up to 8 rooms). Tap a room to sweep it,
  tap its checkbox when done; each room shows findings counted while current.
- **End-of-sweep summary**: findings by category (Cameras, Trackers, Evil
  twins, Karma, Drones, New APs, New BT), then "Discard & exit" — no confirm
  needed, nothing was saved. Home baseline intact, back to ARMED.
- **Settings guards**: re-learn rows hidden during a sweep (a wipe mid-sweep
  would destroy the home baseline); the sweep entry itself only shows when ARMED.

## Still open
- A camera that only joins Wi-Fi as a client never beacons, so no OUI list can see it.
- Sweep findings live in RAM: a reboot mid-sweep loses the session (home baseline unaffected).
- No on-screen restore of `/wd/prelearn.json` or `baseline.bak`.

---
# WatchDog v1.0.2 changes (includes everything from 1.0.1 below)

Fixes from the source review. Compiled-checked only on a host harness
(detectors logic); NOT built for the ESP32 or run on a CYD.

## Fixed
- **Touch**: raw XPT2046 values are now mapped to 320x240 pixels (was clamping every tap to the bottom-right corner).
- **Baseline storage**: temp-file write, read-back verify, then rotate (`baseline.json` -> `.bak`, `.tmp` -> `.json`). Load tries json, tmp, bak. Sightings no longer trigger flash writes. MACs are never duplicated.
- **Re-learn** saves a copy of the old baseline to `/wd/prelearn.json` first.
- **Reboot during learning** resumes learning (remaining time kept in NVS) instead of arming on a partial baseline. Sensitivity persists.
- **Tables never stop recording**: BLE seen (64), APs (48), trackers (16), drones (6) evict the least recently seen entry instead of going blind. Baseline max raised to 200; "baseline full" is shown (learning screen) and Trust refuses to pretend.
- **Tracker counting** restarts after ~10 min without a sighting.
- **Beacon ring** is drained during the listen window (was only at the end, dropping most beacons); ring enlarged; memory barriers added.
- **Twin/Karma** re-alert at most every 15 min per finding (was every beacon).
- **Alert queue + history** (last 16, RAM only): new alerts queue behind an open popup; dismissed alerts stay in the history (footer "Alerts: N >").
- **Ghost-tap guard**: a press that started before a popup appeared cannot hit its buttons.
- **Honest labels**: "Deauth flood?" instead of jamming; non-device popups say "DETECTION - VERIFY".
- **Health**: pill shows NO SCAN if no scan cycle finishes for 2 min; footer shows STORAGE! on save failure.

## Added in 1.0.2
- **Twin baseline persists** (`/wd/twins.bin`, checksummed, saved only on change, at most every 10 min). A rogue already on air at boot is no longer silently adopted. "Clear" on the EvilTwin screen means "I accept this": it forgets those SSIDs so a replaced router stops alerting. Full re-learn also wipes it.
- **Review before arming**: the learning screen has Review (Known list, now showing each device's MAC) and Arm now, which asks for confirmation and warns that anything seen while learning is trusted. Back buttons return to the learning screen while learning.
- **Remove is two-tap** ("Sure?" for 4 s).
- **Look-alike BLE devices**: a fingerprint match is no longer merged if the matched entry was heard in the last 10 s (two devices at once is not a MAC rotation).
- **Camera OUIs**: 14 entries (Hikvision, Dahua, Amcrest, Reolink, Axis added), matched ignoring the locally-administered bit. The added entries are from memory and UNVERIFIED - check them against the IEEE registry.
- **Alert history persists** (`/wd/alerts.json`); restored rows show "prev boot".

## Still open
- Learning still trusts everything seen while it runs; review is now prompted, not enforced.
- A camera that only joins Wi-Fi as a client never beacons, so no OUI list can see it.
- No on-screen restore of `/wd/prelearn.json` or `baseline.bak`.
- BLE merging by fingerprint can still merge a look-alike that appears after the original has been quiet 10 s.
- Not built for the ESP32 or run on a CYD.
