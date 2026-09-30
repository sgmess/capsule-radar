// BOOT-button navigation for touchless boards (the ESP32-S3-LCD-2.8C).
#include "button_input.h"
#include "config.h"
#include <Arduino.h>

#if !BOARD_HAS_TOUCH

// GPIO0 doubles as the strapping pin, but that only matters at reset; at runtime it is
// a plain input with the on-board button pulling it low.
static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t LONG_MS     = 700;

void button_begin() { pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP); }

ButtonEvent button_poll() {
    static bool     wasDown = false;
    static uint32_t downAt = 0;
    static bool     longFired = false;
    const bool down = (digitalRead(PIN_BOOT_BUTTON) == LOW);
    const uint32_t now = millis();
    ButtonEvent ev = BTN_NONE;
    if (down && !wasDown) {
        downAt = now;
        longFired = false;
    } else if (down && !longFired && now - downAt >= LONG_MS) {
        longFired = true;              // fire on the threshold, not on release
        ev = BTN_LONG;
    } else if (!down && wasDown) {
        if (!longFired && now - downAt >= DEBOUNCE_MS) ev = BTN_CLICK;
    }
    wasDown = down;
    return ev;
}

#endif  // !BOARD_HAS_TOUCH
