#pragma once
// Board: Waveshare ESP32-S3-Touch-LCD-2.8C (the TOUCH 2.8" round board).
// Identical to the non-touch ESP32-S3-LCD-2.8C — same ST7701 panel, same init sequence,
// same RGB bus, same expander map — plus a GT911 capacitive touch controller. So this
// header takes the whole 2.8C description and only swaps the BOOT-button navigation
// for real touch.
//
// Checked against the Waveshare wiki pin table and the factory demo for THIS board
// (ESP32-S3-Touch-LCD-2.8C-Demo.zip, Arduino/examples/LVGL_Arduino: Touch_GT911.*,
// Display_ST7701.*): every panel pin, the RGB timing and the 41-command ST7701 init
// sequence match the non-touch demo byte for byte.
#include "board_lcd_28c.h"

#undef  BOARD_NAME
#define BOARD_NAME          "ESP32-S3-Touch-LCD-2.8C"
#undef  BOARD_PIO_ENV
#define BOARD_PIO_ENV       "esp32-s3-touch-lcd-28c"

// Touch wakes the screen, so the normal idle auto-dim applies again.
#undef  IDLE_DIM_MS

// ---------- Touch ----------
#undef  BOARD_HAS_TOUCH
#define BOARD_HAS_TOUCH     1
#define TOUCH_DRIVER_GT911  1
// GT911 address is latched from the INT level while reset is released: INT held low
// selects 0x5D, which is what the vendor demo does (touch_gt911.cpp repeats it).
#define I2C_ADDR_TOUCH      0x5D
#define PIN_TP_INT          16             // GPIO; also the address-select strap at reset
#define EXIO_TP_RST         2              // reset lives on the TCA9554 expander
// The vendor demo feeds raw GT911 coordinates straight to LVGL: they already match
// the panel orientation.
#define TP_MIRROR_X         false
#define TP_MIRROR_Y         false
