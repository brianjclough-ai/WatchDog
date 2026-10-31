// WatchDog v1 — dashboard: the always-up main screen.
// 3x3 tile grid matching the approved mockup.
#pragma once
#include "wd_state.h"

void uiDashboardDraw();
bool uiDashboardTouch(int x, int y);  // true = consumed
