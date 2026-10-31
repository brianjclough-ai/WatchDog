// WatchDog v1 — known devices: baseline list with trusted checkmarks,
// per-device radio type, Rename (on-screen keyboard) / Remove.
#pragma once
#include "wd_state.h"

void uiKnownDraw();
bool uiKnownTouch(int x, int y);  // true = consumed
void uiKnownReset();

void uiKeyboardDraw();
bool uiKeyboardTouch(int x, int y);
// Shared text entry: opens the keyboard for renaming a device or naming a
// new sweep room (purpose decides where OK/Back return).
void uiKeyboardStart(WdKbPurpose purpose, const char* initial, int editIdx);
