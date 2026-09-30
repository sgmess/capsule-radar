#pragma once
// Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C — 3.4" round IPS, 800x800, MIPI-DSI.
//
// STATUS: running on hardware (P4 silicon v3.2): panel, radar UI, GT9271 touch, Wi-Fi via
// the C6, live data. Audio is still compiled out.
//
// A different SoC from every other board here: ESP32-P4 (RISC-V, dual-core 360 MHz,
// 32 MB PSRAM / 32 MB flash) with NO radio of its own. Wi-Fi comes from an ESP32-C6
// co-processor over SDIO (esp_hosted); plain WiFi.h works on top of it. The panel is a
// JD9365 driven over 2-lane MIPI-DSI, which owns its framebuffer (no GRAM windows, no
// scanout compositor). See panel_dsi.cpp and the BOARD_PANEL_DSI path in display.cpp.
//
// Provenance. Every panel value below agrees across THREE independent sources:
//   [BSP]  Waveshare's ESP-IDF BSP waveshare/esp32_p4_wifi6_touch_lcd_xc 3.0.1
//          (CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH), used by all their IDF examples;
//   [DEMO] the vendor Arduino library, examples/arduino/libraries/displays/
//          displays_config.h, SCREEN_3INCH_4_DSI branch (github.com/waveshareteam/
//          ESP32-P4-WIFI6-Touch-LCD-XC);
//   [EW]   EmbeddedWizardGUI/ESP32-P4-WIFI6-Touch-LCD-3.4C, a working IDF project.
// The 197-command init table is identical in all three (panel_dsi_init_34c.h).
// Board-level values (I2C, SDIO link, backlight polarity) are shared with the 4C on the
// same XC PCB and are confirmed on hardware by Skyglass's 4C port
// (github.com/SilentWolf75/skyglass, docs/PORT_P4.md).
//
// NOT the 4C (720x720): its init table differs (register 0x40 = 0x04 there, 0x00 here),
// so flashing this image on a 4C gives a wrong or blank picture.

#define BOARD_NAME          "ESP32-P4-WIFI6-Touch-LCD-3.4C"
#define BOARD_PIO_ENV       "esp32-p4-lcd-34c"      // used in the OTA hint printed at boot

// ---------- Screen ----------
#define SCREEN_W            800            // [BSP] BSP_LCD_H_RES
#define SCREEN_H            800
#define SCREEN_CX           400
#define SCREEN_CY           400
#define RADAR_R_OUTER_PX    374            // 218 * 800/466: the AMOLED's scope proportions
// The UI is laid out on the 466 px AMOLED; UI_S() in ui.cpp scales every hand-placed
// position/size by this factor (permille). 800/466 = 1.717.
#define UI_SCALE_PERMILLE   1717
#define UI_BIG_PANEL        1              // step LVGL fonts up a tier (sizes are discrete)

// ---------- Panel (MIPI-DSI, JD9365) ----------
#define BOARD_PANEL_DSI     1              // selects the esp_lcd MIPI-DSI backend in display.cpp
#define PIN_LCD_RST         27             // [BSP] BSP_LCD_RST, [DEMO] .lcd_rst, [EW]
#define PIN_LCD_BL          26             // [BSP] BSP_LCD_BACKLIGHT, [EW]
// Backlight is ACTIVE LOW: [BSP] configures its LEDC channel with output_invert = 1
// (Skyglass found the same the hard way on the 4C: driving it "on" high keeps it dark).
#define LCD_BL_ACTIVE_LOW   1
#define DSI_LANES           2              // [BSP] BSP_LCD_MIPI_DSI_LANE_NUM
#define DSI_LANE_MBPS       1500           // [BSP] BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS
#define DSI_DPI_CLK_MHZ     80             // [BSP] dpi_clock_freq_mhz, [DEMO] prefer_speed
                                           //   ([EW] runs 44 MHz; 80 is the vendor value)
#define DSI_HSYNC_PULSE     20             // [BSP] = [DEMO] = [EW]
#define DSI_HSYNC_BACK      20
#define DSI_HSYNC_FRONT     40
#define DSI_VSYNC_PULSE     4
#define DSI_VSYNC_BACK      12
#define DSI_VSYNC_FRONT     24
// The DSI PHY runs from an internal LDO that must be switched on explicitly; forgetting
// it is the classic "everything logs fine, screen stays black" failure.
#define DSI_LDO_CHANNEL     3              // [BSP] "LDO_VO3 is connected to VDD_MIPI_DPHY"
#define DSI_LDO_MV          2500

// ---------- Shared I2C (touch + audio codec) ----------
#define PIN_I2C_SDA         7              // [BSP] BSP_I2C_SDA, [DEMO], [EW]
#define PIN_I2C_SCL         8
// [DEMO] runs the bus at 100 kHz (.i2c_clock_speed); [BSP] allows 400 kHz. Stay with
// the slower, proven value.
#define I2C_CLOCK_HZ        100000

// ---------- Touch ----------
// GT911-family over I2C. [BSP] leaves both TP_RST and TP_INT unconnected (GPIO_NUM_NC),
// so there is no reset/strap sequence: the driver probes both Goodix addresses.
// (On the 4C Skyglass found the part is actually a GT9271; the driver accepts any
// Goodix "9xxx" product id for that reason.)
#define BOARD_HAS_TOUCH     1
#define TOUCH_DRIVER_GT911  1
#define I2C_ADDR_TOUCH      0x5D           // [DEMO] ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS
#define I2C_ADDR_TOUCH_ALT  0x14           //   ..._ADDRESS_BACKUP
#define PIN_TP_INT          -1             // [BSP] BSP_LCD_TOUCH_INT = NC
#define PIN_TP_RST          -1             // [BSP] BSP_LCD_TOUCH_RST = NC
#define TP_MIRROR_X         false          // [BSP] swap_xy / mirror_x / mirror_y all 0
#define TP_MIRROR_Y         false

// ---------- Networking ----------
// No native radio: Wi-Fi is the ESP32-C6 over SDIO (esp_hosted; the core's defaults of
// CLK 18 / CMD 19 / D0-D3 14-17 / C6 reset 54 match this board). WiFiManager cannot
// build here (it needs a native PHY), so wifi_setup.cpp provides the setup AP instead.
#define BOARD_HAS_WIFIMANAGER 0

// ---------- Peripherals ----------
// No PMIC, IMU or RTC on the XC board (their drivers probe, find nothing, and the UI
// hides what they feed). Audio is an ES8311 like the 1.75 but on the P4's I2S pins;
// it stays off until the P4 I2S path is verified — see docs/HARDWARE.md.
#define BOARD_HAS_PMIC      0
#define BOARD_HAS_AUDIO     0
#define PIN_I2S_MCLK        13             // [BSP] BSP_I2S_MCLK   (for when audio is enabled)
#define PIN_I2S_BCLK        12             // [BSP] BSP_I2S_SCLK
#define PIN_I2S_LRCLK       10             // [BSP] BSP_I2S_LCLK
#define PIN_I2S_DOUT        9              // [BSP] BSP_I2S_DOUT
#define PIN_I2S_DIN         11             // [BSP] BSP_I2S_DSIN
#define PIN_AUDIO_PA        53             // [BSP] BSP_POWER_AMP_IO
#define I2C_ADDR_IMU        0x6B           // probed, absent on this board
#define I2C_ADDR_RTC        0x51           // probed, absent on this board

#define PIN_BOOT_BUTTON     -1             // not needed: touch navigates (not in the BSP either)
