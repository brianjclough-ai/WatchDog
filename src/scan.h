// WatchDog v1 — scan: time-sliced radio scheduler.
// The ESP32 has one 2.4 GHz radio, so each cycle runs three phases in
// order: (1) BLE advert scan via NimBLE, (2) WiFi AP sweep via
// scanNetworks, (3) promiscuous listen hopping channels 1/6/11 to count
// deauth/disassoc frames and feed beacons + probe-responses to the
// TwinWatch engine. Results flow into detectors.* as they arrive.
#pragma once

void scanInit();
void scanTick();  // call every loop; non-blocking
