// GT911 capacitive touch over I2C (Arduino) — Waveshare ESP32-S3-Touch-LCD-2.8C.
// Protocol per the Waveshare factory demo (Touch_GT911.cpp): 16-bit register addresses,
// status byte at 0x814E (bit7 = buffer ready, low nibble = point count), 8-byte point
// records from 0x814F, and the status byte must be written back to 0 after every read
// or the chip stops reporting. Single-touch is all the radar UI needs.
//
// Polled from the LVGL indev (no interrupt): the INT pin is only used as the address
// strap during reset. Reset is on the TCA9554 expander, so tca9554_begin() (called by
// display::begin() for the panel) must already have run — it has, touch_begin() comes
// after the panel in display::begin().
#include "touch.h"
#include "config.h"

#if TOUCH_DRIVER_GT911
#include "tca9554.h"
#include <Arduino.h>
#include <Wire.h>

#define GT_REG_STATUS      0x814E  // [ready:1][large:1][prox:1][key:1][count:4]
#define GT_REG_POINT1      0x814F  // [track id][xl][xh][yl][yh][sizel][sizeh][rsvd]
#define GT_REG_PRODUCT_ID  0x8140  // ASCII, "911"
// The GT911 refreshes at ~100 Hz but LVGL polls every 20 ms, so a poll can land between
// reports (status not ready). Treat that as "no news": keep the last press for this
// long instead of flickering to released mid-drag.
#define GT_HOLD_MS         60

static bool     s_down = false;
static uint16_t s_x = 0, s_y = 0;
static uint32_t s_lastReportMs = 0;

static bool gt_read(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission((uint8_t)I2C_ADDR_TOUCH);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((uint8_t)I2C_ADDR_TOUCH, len) < len) return false;
    for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

static void gt_clear_status() {
    Wire.beginTransmission((uint8_t)I2C_ADDR_TOUCH);
    Wire.write((uint8_t)(GT_REG_STATUS >> 8));
    Wire.write((uint8_t)GT_REG_STATUS);
    Wire.write((uint8_t)0);
    Wire.endTransmission(true);
}

bool touch_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);   // idempotent (the expander began it)

    // Reset with INT held low -> I2C address 0x5D (vendor sequence and timings).
    pinMode(PIN_TP_INT, OUTPUT);
    digitalWrite(PIN_TP_INT, LOW);
    tca9554_set(EXIO_TP_RST, false);
    delay(10);
    tca9554_set(EXIO_TP_RST, true);
    delay(200);
    digitalWrite(PIN_TP_INT, HIGH);
    pinMode(PIN_TP_INT, INPUT);

    uint8_t id[4] = {0};
    if (gt_read(GT_REG_PRODUCT_ID, id, 3)) {
        Serial.printf("[touch] GT911 responding (product id \"%c%c%c\")\n", id[0], id[1], id[2]);
    } else {
        // Not fatal: touch_read() keeps polling, and the UI still works without touch.
        Serial.println("[touch] GT911 not responding at 0x5D (will keep polling)");
    }
    return true;
}

bool touch_read(uint16_t *ox, uint16_t *oy) {
    uint8_t st = 0;
    if (!gt_read(GT_REG_STATUS, &st, 1)) return false;

    if ((st & 0x80) == 0) {                            // no new report since last poll
        if (s_down && millis() - s_lastReportMs > GT_HOLD_MS) s_down = false;
    } else {
        const uint8_t points = st & 0x0F;
        uint8_t p[8];
        if (points >= 1 && points <= 5 && gt_read(GT_REG_POINT1, p, sizeof(p))) {
            uint16_t x = (uint16_t)(p[2] << 8 | p[1]);
            uint16_t y = (uint16_t)(p[4] << 8 | p[3]);
            if (x > SCREEN_W - 1) x = SCREEN_W - 1;
            if (y > SCREEN_H - 1) y = SCREEN_H - 1;
            if (TP_MIRROR_X) x = (SCREEN_W - 1) - x;
            if (TP_MIRROR_Y) y = (SCREEN_H - 1) - y;
            s_x = x;
            s_y = y;
            s_down = true;
        } else {
            s_down = false;                            // ready with 0 points = finger lifted
        }
        s_lastReportMs = millis();
        gt_clear_status();                             // required, or no further reports
    }

    if (!s_down) return false;
    *ox = s_x;
    *oy = s_y;
    return true;
}

#endif  // TOUCH_DRIVER_GT911
