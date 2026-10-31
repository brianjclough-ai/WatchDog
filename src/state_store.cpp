// WatchDog v1.0.1 — persistent learn/sensitivity state (NVS).
#include "state_store.h"
#include <Preferences.h>

WdSaved wdStateLoad() {
  WdSaved s = {false, false, false, 0, 1};
  Preferences p;
  if (!p.begin("wd", true)) return s;
  s.valid = p.getUChar("v", 0) == 1;
  if (s.valid) {
    s.learning = p.getBool("learn", false);
    s.additive = p.getBool("add", false);
    s.remain   = p.getUInt("rem", 0);
    s.sens     = p.getUChar("sens", 1);
    if (s.sens > 2) s.sens = 1;
  }
  p.end();
  return s;
}

void wdStateSave(bool learning, bool additive, uint32_t remain, uint8_t sens) {
  Preferences p;
  if (!p.begin("wd", false)) return;
  p.putUChar("v", 1);
  p.putBool("learn", learning);
  p.putBool("add", additive);
  p.putUInt("rem", remain);
  p.putUChar("sens", sens);
  p.end();
}
