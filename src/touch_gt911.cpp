// GT911-family capacitive touch over I2C (Arduino) — Waveshare ESP32-S3-Touch-LCD-2.8C
// and the ESP32-P4-WIFI6-Touch-LCD-XC boards.
// Protocol per the Waveshare factory demo (Touch_GT911.cpp): 16-bit register addresses,
// status byte at 0x814E (bit7 = buffer ready, low nibble = point count), 8-byte point
// records from 0x814F, and the status byte must be written back to 0 after every read
// or the chip stops reporting. Single-touch is all the radar UI needs.
//
// Polled from the LVGL indev (no interrupt). Where the board wires the reset (the 2.8C:
// on the TCA9554 expander, EXIO_TP_RST), the INT pin straps the address during reset;
// tca9554_begin() has already run by then because display::begin() brings the panel up
// before touch_begin(). Where reset/INT are not connected (the P4 XC boards), there is
// no strap, so both Goodix addresses are probed. Any Goodix "9xxx" product id is
// accepted: Skyglass found a GT9271 behind the "GT911" label on the P4 4C.
#include "touch.h"
#include "config.h"

#if TOUCH_DRIVER_GT911
#ifdef EXIO_TP_RST
#include "tca9554.h"
#endif
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
static uint8_t  s_addr = I2C_ADDR_TOUCH;

static bool gt_read(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(s_addr, len) < len) return false;
    for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

static void gt_clear_status() {
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(GT_REG_STATUS >> 8));
    Wire.write((uint8_t)GT_REG_STATUS);
    Wire.write((uint8_t)0);
    Wire.endTransmission(true);
}

bool touch_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);   // idempotent: the first begin sets the clock

#ifdef EXIO_TP_RST
    // Reset with INT held low -> I2C address 0x5D (vendor sequence and timings).
    pinMode(PIN_TP_INT, OUTPUT);
    digitalWrite(PIN_TP_INT, LOW);
    tca9554_set(EXIO_TP_RST, false);
    delay(10);
    tca9554_set(EXIO_TP_RST, true);
    delay(200);
    digitalWrite(PIN_TP_INT, HIGH);
    pinMode(PIN_TP_INT, INPUT);
#endif

#ifdef I2C_ADDR_TOUCH_ALT
    const uint8_t addrs[2] = { I2C_ADDR_TOUCH, I2C_ADDR_TOUCH_ALT };   // no strap: try both
#else
    const uint8_t addrs[1] = { I2C_ADDR_TOUCH };
#endif
    uint8_t id[4] = {0};
    for (uint8_t a : addrs) {
        s_addr = a;
        if (gt_read(GT_REG_PRODUCT_ID, id, 4) && id[0] == '9') {
            Serial.printf("[touch] Goodix GT%c%c%c%c at 0x%02X\n", id[0], id[1], id[2],
                          id[3] ? id[3] : ' ', a);
            return true;
        }
    }
    // Not fatal: touch_read() keeps polling the default address, and the UI still works.
    s_addr = I2C_ADDR_TOUCH;
    Serial.printf("[touch] no Goodix controller answered (will keep polling 0x%02X)\n", s_addr);
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
