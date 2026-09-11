// FT3168 capacitive touch over I2C (Arduino) — Waveshare ESP32-S3-Touch-AMOLED-1.43.
//
// Standard FocalTech register layout: one burst read from 0x00 gives the touch count
// followed by 6 bytes per point. Two points are read so the pinch gesture works, matching
// the CST9217 driver's contract.
//
// This chip has no reset line of its own on this board -- it is wired to the panel reset --
// so it only answers on I2C once the display has been initialised. display::begin() calls
// gfx->begin() before touch_begin(), which is what makes this work.
#include "config.h"

#if TOUCH_DRIVER_FT3168
#include "touch_ft3168.h"
#include <Arduino.h>
#include <Wire.h>

#define FT_REG_DATA      0x00     // [mode][gest][count] then 6 bytes per point
#define FT_REG_CHIPID    0xA3     // reads 0x64 on the FT3168
#define FT_CHIPID        0x64
#define FT_HDR_LEN       3
#define FT_POINT_LEN     6
#define FT_DATA_LEN      (FT_HDR_LEN + 2 * FT_POINT_LEN)   // header + two points
#define FT_EVT_LIFT_UP   1        // top 2 bits of the point's first byte

static bool ft_read_reg(uint8_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission((uint8_t)I2C_ADDR_TOUCH);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;   // repeated START
    if (Wire.requestFrom((uint8_t)I2C_ADDR_TOUCH, len) < len) return false;
    for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

// Unpack one 6-byte point record. Returns false for a lift-up event, so a finger that is
// leaving is not reported as still down.
static bool ft_unpack_point(const uint8_t *p, uint16_t *ox, uint16_t *oy) {
    if ((p[0] >> 6) == FT_EVT_LIFT_UP) return false;

    uint16_t x = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
    uint16_t y = (uint16_t)(((p[2] & 0x0F) << 8) | p[3]);

    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    if (y > SCREEN_H - 1) y = SCREEN_H - 1;
    if (TP_MIRROR_X) x = (SCREEN_W - 1) - x;
    if (TP_MIRROR_Y) y = (SCREEN_H - 1) - y;

    *ox = x;
    *oy = y;
    return true;
}

bool touch_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    uint8_t id = 0;
    if (ft_read_reg(FT_REG_CHIPID, &id, 1) && id == FT_CHIPID) {
        Serial.println("[touch] FT3168 responding (chip id 0x64)");
    } else {
        // Not fatal: the panel reset may still be settling. Reads simply retry.
        Serial.printf("[touch] FT3168 not responding yet (id=0x%02X, will keep polling)\n", id);
    }
    return true;
}

int touch_read_points(uint16_t x[2], uint16_t y[2]) {
    uint8_t d[FT_DATA_LEN];
    if (!ft_read_reg(FT_REG_DATA, d, FT_DATA_LEN)) return 0;

    const uint8_t points = d[2] & 0x0F;
    if (points == 0 || points > 5) return 0;      // 0x0F means "no valid data"

    int n = 0;
    if (ft_unpack_point(d + FT_HDR_LEN, &x[n], &y[n])) ++n;
    if (points >= 2 && ft_unpack_point(d + FT_HDR_LEN + FT_POINT_LEN, &x[n], &y[n])) ++n;
    return n;
}

bool touch_read(uint16_t *ox, uint16_t *oy) {
    uint16_t xs[2], ys[2];
    if (touch_read_points(xs, ys) < 1) return false;
    *ox = xs[0];
    *oy = ys[0];
    return true;
}

#endif  // TOUCH_DRIVER_FT3168
