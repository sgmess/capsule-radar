#pragma once
// BOOT-button input for touchless boards (BOARD_HAS_TOUCH 0, e.g. the 2.8C): the only on-device control.
// main.cpp maps CLICK to "next view" and LONG to "next range step".
enum ButtonEvent { BTN_NONE = 0, BTN_CLICK, BTN_LONG };

void button_begin();
ButtonEvent button_poll();   // call every loop() iteration; debounced
