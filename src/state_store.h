// WatchDog v1.0.1 — tiny persistent state in NVS: learning progress and
// sensitivity survive a reboot. (Uptime-based timers reset on power loss, so
// the REMAINING learn time is stored, not the end time.)
#pragma once
#include <stdint.h>

struct WdSaved {
  bool     valid;      // false on first boot / after erase
  bool     learning;
  bool     additive;
  uint32_t remain;     // seconds of learning left
  uint8_t  sens;       // 0..2
};

WdSaved wdStateLoad();
void    wdStateSave(bool learning, bool additive, uint32_t remain, uint8_t sens);
