// WatchDog v1.0.1 — alert history: the last 16 alerts (newest first), kept
// even after the popup is dismissed. Tap a row to open that detector's detail.
#pragma once

void uiHistoryDraw();
bool uiHistoryTouch(int x, int y);  // true = consumed
void uiHistoryReset();
