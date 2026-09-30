#pragma once
// Board: Waveshare ESP32-S3-LCD-2.8C (the NON-touch 2.8" round board).
// ESP32-S3R8 (8 MB PSRAM / 16 MB flash), ST7701 480x480 round IPS over 16-bit parallel
// RGB, TCA9554 IO expander, QMI8658 IMU, PCF85063 RTC, active buzzer, simple charger +
// ADC divider.  No touch, no PMIC, no audio codec.
//
// The ESP32-S3-Touch-LCD-2.8C is this same board plus a GT911 touch controller; it has
// its own header (board_touch_lcd_28c.h) that builds on this one.
//
// Everything about this panel differs from the AMOLED boards: the ST7701 has no GRAM,
// so the S3 continuously scans a full framebuffer out of PSRAM over the RGB bus, and
// the panel only takes its init sequence over a 3-wire SPI whose CS and reset live on
// the IO expander. See panel_st7701.cpp and the BOARD_PANEL_ST7701 path in display.cpp.
//
// Pins are copied from the Waveshare factory demo (ESP32-S3-LCD-2.8C-Demo.zip,
// Arduino/examples/LVGL_Arduino: Display_ST7701.h, I2C_Driver.h, BAT_Driver.h,
// Button_Driver.h; buzzer bit from the ESP-IDF demo's Buzzer.c), and run on real
// hardware in @ijord's port (https://github.com/ijord/capsule-radar), which this
// board support is lifted from.

#define BOARD_NAME          "ESP32-S3-LCD-2.8C"
#define BOARD_PIO_ENV       "esp32-s3-lcd-28c"      // used in the OTA hint printed at boot

// ---------- Screen ----------
// Overrides the 466x466 AMOLED defaults in config.h.
#define SCREEN_W            480
#define SCREEN_H            480
#define SCREEN_CX           240
#define SCREEN_CY           240
#define RADAR_R_OUTER_PX    225            // 218 * 480/466: same scope proportions as the AMOLED
// Nothing to wake the screen with but the BOOT button, so never auto-dim by default
// (still settable from the web page).
#define IDLE_DIM_MS         0

// ---------- Panel ----------
#define BOARD_PANEL_ST7701  1              // selects the esp_lcd RGB backend in display.cpp
#define PIN_LCD_SCLK        2              // ST7701 3-wire SPI clock (init sequence only)
#define PIN_LCD_MOSI        1              // ST7701 3-wire SPI data (init sequence only)
#define PIN_LCD_BL          6              // backlight, LEDC PWM (20 kHz / 10-bit, from the demo)

// 16-bit parallel RGB565 bus (esp_lcd RGB panel):
#define PIN_RGB_HSYNC       38
#define PIN_RGB_VSYNC       39
#define PIN_RGB_DE          40
#define PIN_RGB_PCLK        41
#define PIN_RGB_D0          5
#define PIN_RGB_D1          45
#define PIN_RGB_D2          48
#define PIN_RGB_D3          47
#define PIN_RGB_D4          21
#define PIN_RGB_D5          14
#define PIN_RGB_D6          13
#define PIN_RGB_D7          12
#define PIN_RGB_D8          11
#define PIN_RGB_D9          10
#define PIN_RGB_D10         9
#define PIN_RGB_D11         46
#define PIN_RGB_D12         3
#define PIN_RGB_D13         8
#define PIN_RGB_D14         18
#define PIN_RGB_D15         17

// RGB timings: porches verbatim from the demo. The pixel clock is NOT the demo's 30 MHz:
// that runs the panel at ~107 Hz and saturates PSRAM, so WiFi/TLS bursts starve the
// scanout and the image drifts sideways. 16 MHz (~57 Hz) is what ran clean for weeks on
// @ijord's board; 18 MHz was marginal once the sweep compositor's PSRAM traffic landed.
// CONFIG_LCD_RGB_RESTART_IN_VSYNC (platformio.ini) self-heals transient underruns.
#define RGB_PCLK_HZ         16000000
#define RGB_HSYNC_PW        8
#define RGB_HSYNC_BP        10
#define RGB_HSYNC_FP        50
#define RGB_VSYNC_PW        2
#define RGB_VSYNC_BP        18
#define RGB_VSYNC_FP        8

// TCA9554 IO expander (I2C) drives the LCD control lines + buzzer (EXIO numbering 1..8):
#define I2C_ADDR_EXPANDER   0x20           // TCA9554PWR
#define EXIO_LCD_RST        1              // ST7701 reset
#define EXIO_LCD_CS         3              // ST7701 3-wire SPI chip select
#define EXIO_BUZZER         8              // active buzzer: high = on

// ---------- Shared I2C (IO expander + IMU + RTC) ----------
// Fixed on the board's I2C header too (GPIO7 SCL / GPIO15 SDA per the Waveshare docs).
#define PIN_I2C_SDA         15
#define PIN_I2C_SCL         7

// ---------- Touch ----------
// None. The BOOT button stands in: click = next view, long press = next range step
// (button_input.cpp, wired up in main.cpp).
#define BOARD_HAS_TOUCH     0

// ---------- Peripherals ----------
#define BOARD_HAS_PMIC      0              // no AXP2101: battery is read from an ADC divider
#define BOARD_HAS_AUDIO     0              // no ES8311 codec / speaker...
#define BOARD_HAS_BUZZER    1              // ...just an on/off buzzer on EXIO_BUZZER
#define BOARD_HAS_BATT_ADC  1
#define PIN_BAT_ADC         4              // battery voltage divider (analogReadMilliVolts)
#define BAT_VOLT_SCALE      3.0f           // divider ratio (from the demo's BAT_Driver)
#define BAT_VOLT_OFFSET     0.980952f      // demo's calibration fudge factor
#define I2C_ADDR_IMU        0x6B
#define I2C_ADDR_RTC        0x51

#define PIN_BOOT_BUTTON     0              // BOOT button: the only input on this board
