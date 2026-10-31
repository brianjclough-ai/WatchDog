// WatchDog v1 — alert popup: NEW DEVICE dialog drawn over the dashboard.
// Trust baselines the device, Watch snoozes it for an hour, Details opens
// the tile's detail screen. The Trust button only appears when the alert
// is tagged with a device identity (radio != 255).
#pragma once

void uiAlertDraw();              // draws over whatever is on screen
bool uiAlertTouch(int x, int y); // true = consumed
