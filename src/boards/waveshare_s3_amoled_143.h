#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-1.43.
//
// ESP32-S3-PICO, 8 MB PSRAM, 16 MB flash. The SAME CO5300 466x466 AMOLED over QSPI as
// the 1.75, so the whole display path is shared -- but the pin map has nothing in common
// with it, touch is an FT3168 rather than a CST9217, and there is no PMIC and no audio
// codec. Flashing a 1.75 image here gives a black screen and a completely silent I2C bus.
//
// Pins come from the Arduino-ESP32 core's own board variant
// (variants/waveshare_esp32_s3_touch_amoled_143/pins_arduino.h, shared with the 1.64) and
// were then confirmed on real hardware: an I2C scan on 47/48 answers 0x51 + 0x6B + 0x38,
// and the panel drives correctly on CS=9 SCK=10 D0..D3=11..14 RST=21.
//
// Do NOT "correct" these against the Waveshare wiki pin table: that table is misaligned
// and lists an I2C SCL of GPIO49, which does not exist on an ESP32-S3.

#define BOARD_HOSTNAME      "skyglass-143"
#define BOARD_SETUP_AP      "SkyGlass-143-Setup"
#define BOARD_NAME          "Waveshare ESP32-S3-Touch-AMOLED-1.43"
#define BOARD_PIO_ENV       "esp32-s3-amoled-143"   // used in the OTA hint printed at boot
#define BOARD_PANEL_QSPI    1        // Arduino_GFX + CO5300 over QSPI, same as the 1.75
#define BOARD_PANEL_DSI     0

// ---------- Screen geometry ----------
// Identical panel to the 1.75, so every one of these matches that board, including the
// memory-driven UI limits: same 8 MB PSRAM and the same tight internal-RAM budget.
#define SCREEN_W            466
#define SCREEN_H            466
#define SCREEN_CX           233
#define SCREEN_CY           233
#define RADAR_R_OUTER_PX    218
#define WX_RADAR_SIZE       466
#define WX_RADAR_FRAMES     6
#define UI_LIST_MAX_ROWS    12
#define LCD_COL_OFFSET      6              // same CO5300 column gap as the 1.75; verified
#define LCD_ROW_OFFSET      0
// Vendor rate. Measured on this board: 40 MHz gives 6.8 fps against 7.4 at 80 (n=11 each,
// ranges 6-8 vs 6-9, p~0.17 -- noise), because the frame rate is bound by LVGL's
// anti-aliased rendering, not QSPI bandwidth. The faster clock buys nothing measurable
// and only adds signal-integrity risk.
#define LCD_QSPI_HZ         40000000

// ---------- Panel / touch ----------
#define PIN_LCD_CS          9
#define PIN_LCD_RST         21
#define PIN_LCD_SCLK        10             // QSPI PCLK
#define PIN_LCD_D0          11
#define PIN_LCD_D1          12
#define PIN_LCD_D2          13
#define PIN_LCD_D3          14

// The FT3168 has no reset line of its own: it is tied to the panel reset (GPIO21), so it
// does not appear on I2C at all until the display has been initialised. display::begin()
// already calls gfx->begin() before touch_begin(); do not reorder those two.
#define PIN_TP_INT          -1             // not broken out
#define PIN_TP_RST          -1             // shared with PIN_LCD_RST
// Raw controller coordinates already match the panel orientation (verified: the on-screen
// dot lands under the fingertip; x spans 24..443, y spans 0..459). The 1.75 mirrors both.
#define TP_MIRROR_X         false
#define TP_MIRROR_Y         false
#define TOUCH_DRIVER_FT3168 1

// ---------- Shared I2C (touch + IMU + RTC) ----------
#define PIN_I2C_SDA         47
#define PIN_I2C_SCL         48

#define PIN_BOOT_BUTTON     0

// ---------- I2C addresses ----------
#define I2C_ADDR_TOUCH      0x38           // FT3168; chip-id register 0xA3 reads 0x64
#define I2C_ADDR_IMU        0x6B
#define I2C_ADDR_RTC        0x51

// ---------- Peripherals present ----------
// No AXP2101 and no ES8311 here (an I2C scan finds neither). Battery level is instead a
// divided analog reading on GPIO4, but the divider ratio is undocumented, so battery
// reporting stays off rather than showing an invented number.
#define BOARD_HAS_IMU       1
#define BOARD_HAS_RTC       1
#define BOARD_HAS_PMIC      0
#define BOARD_HAS_SD        0
#define BOARD_HAS_AUDIO     0
#define BOARD_HAS_WIFIMANAGER 1
#define PIN_BAT_ADC         4              // unused; see note above

#if (PIN_LCD_SCLK < 0) || (PIN_I2C_SDA < 0)
#  error "board: QSPI/I2C pins are back to placeholders (-1). Restore the real values."
#endif
