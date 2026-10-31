// WatchDog v1 — detector detail: scrollable per-tile finding list.
#pragma once

void uiDetailDraw();
bool uiDetailTouch(int x, int y);  // true = consumed
void uiDetailReset();              // call when opening (resets paging)
