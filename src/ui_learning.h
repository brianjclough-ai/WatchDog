// WatchDog v1 — learning progress + settings.
#pragma once

void uiLearningDraw();
bool uiLearningTouch(int x, int y);  // true = consumed

void uiSettingsDraw();
bool uiSettingsTouch(int x, int y);

void uiArmConfirmDraw();
bool uiArmConfirmTouch(int x, int y);

void uiConfirmDraw();  // full re-learn confirm dialog
bool uiConfirmTouch(int x, int y);
