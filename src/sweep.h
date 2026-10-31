// WatchDog v1.1.0 — sweep mode: rental/Airbnb checks.
// A temporary high-sensitivity session. The home baseline on LittleFS is
// never consulted and never written; a RAM-only session table takes its
// place (see baselineSession*). Detectors run hot, popups work as normal,
// and Trust lands in the session table only. On exit everything is
// discarded and the unit returns to ARMED with the home baseline intact.
#pragma once
#include "wd_state.h"

void sweepBegin();   // enter SWEEP from Settings (ARMED only)
void sweepEnd();     // discard session, restore, back to ARMED + dashboard
void sweepAddRoom(const char* name);

void uiSweepRoomsDraw();
bool uiSweepRoomsTouch(int x, int y);   // true = consumed
void uiSweepSummaryDraw();
bool uiSweepSummaryTouch(int x, int y); // true = consumed
