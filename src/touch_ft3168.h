#pragma once
// FT3168 capacitive touch (I2C) — Waveshare ESP32-S3-Touch-AMOLED-1.43.
// Same interface as touch_cst9217.h; exactly one driver is compiled per board.
#include <stdint.h>

bool touch_begin();                       // I2C + comms check; logs status
bool touch_read(uint16_t *x, uint16_t *y); // true if pressed (x,y in screen px)
int  touch_read_points(uint16_t x[2], uint16_t y[2]); // 0/1/2 active points (pinch support)
