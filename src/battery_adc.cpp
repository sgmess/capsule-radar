// Battery readout for the ESP32-S3-LCD-2.8C: no fuel-gauge PMIC, just a charger IC and
// a voltage divider on PIN_BAT_ADC (values from the Waveshare demo's BAT_Driver).
// Implements the battery.h API so main.cpp is unchanged.
//
// Heuristics, since a bare voltage can't tell charge state apart:
//  - "charging" = rail above 4.10 V (the charger holds BAT at ~4.2 V whenever USB is in;
//    a freshly-full battery briefly reads the same — the cost is a few minutes at the
//    faster mains poll rate, not a wrong radar).
//  - with USB in and NO battery, the charger output still reaches the divider, so this
//    reads as a full, charging battery. Acceptable: it displays as "plugged in".
#include "battery.h"
#include "config.h"

#if BOARD_HAS_BATT_ADC
#include <Arduino.h>

static bool s_begun = false;

static float railVolts() {
    if (!s_begun) return 0.0f;
    return (analogReadMilliVolts(PIN_BAT_ADC) * BAT_VOLT_SCALE / 1000.0f) / BAT_VOLT_OFFSET;
}

bool battery_begin() {
    analogReadResolution(12);
    s_begun = true;
    return true;
}

bool battery_present() {
    const float v = railVolts();
    return v > 3.0f && v < 4.5f;
}

int battery_percent() {
    const float v = railVolts();
    if (v <= 3.0f || v >= 4.5f) return -1;
    // linear LiPo approximation: 3.30 V empty .. 4.15 V full
    const int pct = (int)((v - 3.30f) * (100.0f / (4.15f - 3.30f)) + 0.5f);
    return constrain(pct, 0, 100);
}

bool battery_charging() { return railVolts() > 4.10f; }

void battery_enable_codec_rail() {}   // no codec (and no PMIC rail) on this board

#endif  // BOARD_HAS_BATT_ADC
