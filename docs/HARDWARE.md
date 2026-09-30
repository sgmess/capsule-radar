# Hardware

Capsule Radar supports three Waveshare round boards on the same ESP32-S3R8 SoC. The two
AMOLEDs share the 466×466 CO5300 panel; the 2.8C is a 480×480 ST7701 IPS on a parallel
RGB bus. Their **pin maps have nothing in common**: flashing one board's image onto
another gives a black screen and a silent I2C bus.

Pin maps live one-per-board in [`../src/boards/`](../src/boards/) and are selected by a
build flag (see [`../src/config.h`](../src/config.h)):

| Board | PlatformIO env | Build flag | Header |
|---|---|---|---|
| ESP32-S3-Touch-AMOLED-1.75 (reference) | `esp32-s3-amoled-175` | *(default)* | `board_amoled_175.h` |
| ESP32-S3-Touch-AMOLED-1.43 | `esp32-s3-amoled-143` | `-DBOARD_AMOLED_143` | `board_amoled_143.h` |
| ESP32-S3-LCD-2.8C (non-touch) | `esp32-s3-lcd-28c` | `-DBOARD_LCD_28C` | `board_lcd_28c.h` |
| ESP32-S3-Touch-LCD-2.8C | `esp32-s3-touch-lcd-28c` | `-DBOARD_TOUCH_LCD_28C` | `board_touch_lcd_28c.h` |
| ESP32-P4-WIFI6-Touch-LCD-3.4C | `esp32-p4-lcd-34c` | `-DBOARD_P4_LCD_34C` | `board_p4_lcd_34c.h` |

---

## ESP32-S3-Touch-AMOLED-1.75 (reference board)

- **MCU**: ESP32-S3R8 (Xtensa LX7 dual-core @240 MHz), 512 KB SRAM, **8 MB PSRAM**, **16 MB flash**.
- **Wireless**: 2.4 GHz WiFi (b/g/n) + Bluetooth 5 (LE), onboard antenna (IPEX option).
- **Display**: 1.75" AMOLED, **466×466**, driver **CO5300** over **QSPI**. Brightness via panel command (no PWM backlight line).
- **Touch**: **CST9217** capacitive, I2C.
- **IMU**: **QMI8658** 6-axis, I2C. **RTC**: **PCF85063**, I2C.
- **PMIC**: **AXP2101**, I2C — LiPo charge + rails.
- **Audio**: **ES8311** codec + onboard speaker; dual-mic array (ES7210 on the -C variant).
- **Storage**: microSD (TF). **Buttons**: PWR + BOOT. **GPS** (`-G` variant only): LC76G.

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| LCD CS | 12 | | I2C SDA | 15 |
| LCD RST | 39 | | I2C SCL | 14 |
| LCD QSPI SCLK | 38 | | I2S MCLK | 42 |
| LCD QSPI D0–D3 | 4, 5, 6, 7 | | I2S BCLK | 9 |
| Touch INT | 11 | | I2S LRCLK | 45 |
| Touch RST | 40 | | I2S DOUT / DIN | 8 / 10 |
| Touch transform | mirror_x + mirror_y | | Speaker amp enable | 46 |

Panel column gap **6**, row gap 0, QSPI clock 80 MHz.
I2C: CST9217 `0x5A`, QMI8658 `0x6B`, PCF85063 `0x51`, AXP2101 `0x34`, ES8311 `0x18`.

Sources: the board's ESPHome definition, the Waveshare board definition in xiaozhi-esp32,
and a working Arduino_GFX port for this panel.

---

## ESP32-S3-Touch-AMOLED-1.43

Same 466×466 CO5300 panel, 8 MB PSRAM / 16 MB flash. Differences that matter:

- **Touch is an FT3168**, not a CST9217, at a different address and with a different protocol.
- **No AXP2101 and no ES8311** — no battery reporting and no alert pings. Both compile out
  via `BOARD_HAS_PMIC` / `BOARD_HAS_AUDIO`. This is not merely cosmetic: the 1.75's I2S
  BCLK/DIN (GPIO 9/10) are *this* board's QSPI CS and SCLK, so leaving audio enabled here
  would drive the panel's own bus lines.
- The FT3168 has **no reset line of its own**; it is tied to the panel reset (GPIO 21) and
  does not appear on I2C at all until the display has been initialised. `display::begin()`
  therefore must keep `gfx->begin()` ahead of `touch_begin()`.
- IMU and RTC are the same parts at the same addresses as the 1.75.

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| LCD CS | 9 | | I2C SDA | 47 |
| LCD RST | 21 | | I2C SCL | 48 |
| LCD QSPI SCLK | 10 | | Touch INT / RST | not broken out |
| LCD QSPI D0–D3 | 11, 12, 13, 14 | | Touch transform | none (raw coords match) |
| Battery ADC | 4 (unused — see below) | | IMU INT / RTC INT | 8 / 15 |

Panel column gap **6** (same as the 1.75), row gap 0, QSPI clock 40 MHz (the vendor rate;
80 MHz is untested here).
I2C: FT3168 `0x38` (chip-id register `0xA3` reads `0x64`), QMI8658 `0x6B`, PCF85063 `0x51`.

Battery: there is a divided battery voltage on GPIO 4, but the divider ratio is not
documented, so the firmware reports "no battery" rather than an invented percentage.

**Sources.** Pins come from the Arduino-ESP32 core's own board variant
(`variants/waveshare_esp32_s3_touch_amoled_143/pins_arduino.h`, shared with the 1.64), and
each was then confirmed on hardware: an I2C scan on 47/48 answers `0x51` + `0x6B` + `0x38`,
and the panel drives correctly on CS=9 / SCK=10 / D0–D3=11–14 / RST=21. The touch orientation
was verified with an on-screen dot tracking a fingertip, and the column gap by nudging a
full-panel rim-ring test card one pixel at a time until it centred.

> Do **not** "correct" these against the Waveshare wiki pin table: that table is misaligned
> and lists an I2C SCL of GPIO 49, which does not exist on an ESP32-S3.

---

## ESP32-S3-LCD-2.8C (non-touch) and ESP32-S3-Touch-LCD-2.8C

Board support comes from [@ijord's port](https://github.com/ijord/capsule-radar), where it
runs on real hardware. Same SoC, 8 MB PSRAM / 16 MB flash; almost everything else differs:

- **Display is an ST7701 480×480 round IPS on a 16-bit parallel RGB bus**, with no GRAM.
  The S3's LCD peripheral scans a full framebuffer out of PSRAM continuously (esp_lcd RGB
  driver, double-buffered, 30-line internal bounce buffers). The panel takes its init
  sequence over a 9-bit 3-wire SPI whose **CS and reset are on the TCA9554 IO expander**.
  Backlight is LEDC PWM on GPIO 6 (20 kHz / 10-bit), not a panel command.
- **Touch depends on the variant.** The *Touch*-LCD-2.8C adds a **GT911** (I2C `0x5D`, INT
  GPIO 16, reset on expander EXIO2; raw coordinates match the panel) and otherwise matches
  the non-touch board exactly: same pins, same RGB timing, and a byte-identical ST7701 init
  sequence in both vendor demos. Its header just includes `board_lcd_28c.h` and adds touch.
  On the **non-touch** board the **BOOT button** navigates: click = next view (radar → list →
  stats → weather), long press (≥ 0.7 s) = next range step. Tap-to-inspect and the
  on-screen zoom button are unreachable there, and auto-dim defaults to off.
- **Buzzer, not a speaker**: an on/off active buzzer on expander pin EXIO8 plays the alert
  patterns (`audio_buzzer.cpp`). No volume control.
- **Battery by ADC divider** on GPIO 4 (×3.0, calibration from the vendor demo), not a PMIC.
  Charging is inferred from the rail voltage (> 4.10 V); USB with no battery reads as
  "charging".
- **Radar sweep is composited outside LVGL** (`compose_step()` in `display.cpp`): LVGL only
  re-renders when data changes, which keeps the PSRAM bus quiet enough for the scanout.
- Needs its own PlatformIO platform (pioarduino 55.03.311 / core 3.3.11) with a custom
  sdkconfig: **XIP from PSRAM** (otherwise the scanout ISR panics on every NVS write),
  IRAM-safe RGB ISR, `CONFIG_LCD_RGB_RESTART_IN_VSYNC` to self-heal underruns, and the
  64-byte data-cache line from Waveshare's own `sdkconfig.defaults` (more PSRAM bandwidth
  for the scanout; the Arduino default is 32 B). Built with `-O2`. The first
  build rebuilds the Arduino core libs and is slow; later builds are cached.
- **The USB-C port is a CH343P USB-UART bridge on UART0** (GPIO 43/44), not the S3's
  native USB, so these envs build with `ARDUINO_USB_CDC_ON_BOOT=0`. With the AMOLED
  default (`=1`) the serial log goes to the native-USB pins and never reaches the cable.
- LVGL's object pool lives in PSRAM on this board (`include/lv_conf.h`) so mbedTLS still
  finds its contiguous internal blocks next to the bounce buffers.

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| RGB HSYNC / VSYNC | 38 / 39 | | I2C SDA | 15 |
| RGB DE / PCLK | 40 / 41 | | I2C SCL | 7 |
| RGB D0–D7 | 5, 45, 48, 47, 21, 14, 13, 12 | | Backlight PWM | 6 |
| RGB D8–D15 | 11, 10, 9, 46, 3, 8, 18, 17 | | Battery ADC | 4 |
| ST7701 SPI SCLK / MOSI | 2 / 1 | | BOOT button | 0 |
| ST7701 RST / CS | EXIO1 / EXIO3 | | Buzzer | EXIO8 |
| Touch INT (Touch variant) | 16 | | Touch RST (Touch variant) | EXIO2 |

RGB timing: PCLK **16 MHz** (~57 Hz). The demo's 30 MHz saturates PSRAM and the image drifts
sideways under WiFi load. HSYNC pw/bp/fp 8/10/50, VSYNC 2/18/8.
I2C: TCA9554 `0x20`, QMI8658 `0x6B`, PCF85063 `0x51`, GT911 `0x5D` (Touch variant).

Sources: the Waveshare factory demo (`ESP32-S3-LCD-2.8C-Demo.zip`, Arduino
`LVGL_Arduino`: `Display_ST7701.h`, `I2C_Driver.h`, `BAT_Driver.h`, `Button_Driver.h`;
buzzer bit from the ESP-IDF demo's `Buzzer.c`), and the Touch-LCD-2.8C factory demo
(`ESP32-S3-Touch-LCD-2.8C-Demo.zip`: `Touch_GT911.*`, ESP-IDF `sdkconfig.defaults`).

---

## ESP32-P4-WIFI6-Touch-LCD-3.4C

**Status: running on hardware** (a 3.4C with ESP32-P4 silicon **v3.2**). Confirmed on the
board: the panel (JD9365 answers `93 65 04`, 197/197 init commands, 800×800 video), the radar
UI, touch (a Goodix **GT9271** at `0x5D`), Wi-Fi through the C6, live ADS-B, the weather
forecast, rain radar and satellite images, the web config page and OTA. Not yet verified:
audio (compiled out), and the weather tile is still its fetched 360 px, centred.

Bring-up lessons, in case the 4C or another P4 board comes next:
- **The DSI PHY clock source must be `0`** ("pick the default for this silicon"), as
  Waveshare's code passes. `MIPI_DSI_PHY_CLK_SRC_DEFAULT` is the legacy PLL_F20M reference,
  which v3.x silicon lacks: `esp_lcd_new_dsi_bus()` aborts, giving a lit, black panel.
- **Logging goes to the P4's native USB** (`303a:1001`, "USB JTAG/serial debug unit",
  `/dev/ttyACM0` on Linux), which is also the flashing port, so `ARDUINO_USB_CDC_ON_BOOT=1`.
  With `=0` Arduino's `Serial` goes to the CH343P UART instead and only the IDF's own
  `E (…)` lines mirror to the native port. Early boot lines are easy to miss because the
  port re-enumerates on reset.
- `E rpc_core: Response not received for … Req_GetCoprocessorFwVersion` at boot is harmless:
  the core asks the C6 for its firmware version, which the C6's older ESP-Hosted firmware
  does not answer. Wi-Fi works regardless.

A different chip from every other board here:

- **SoC: ESP32-P4** (RISC-V, dual-core 360 MHz), 32 MB PSRAM, 32 MB flash. Different
  toolchain (RISC-V), different image layout (**bootloader at `0x2000`**, not `0x0`), so
  flash it with `esptool --chip esp32p4`. Only the first 16 MB of flash is partitioned.
- **No radio of its own.** Wi-Fi comes from an **ESP32-C6 over SDIO** via `esp_hosted`; plain
  `WiFi.h` works on top of it. The core's default SDIO pins (CLK 18, CMD 19, D0–D3 14–17, C6
  reset 54) match this board. **WiFiManager cannot build without a native PHY**, so
  `wifi_setup.cpp` provides the setup instead: the `CapsuleRadar-Setup` AP with a captive
  page, or over serial: `wifi <ssid> <password>`.
- **Display: 3.4" round 800×800 IPS, JD9365 over 2-lane MIPI-DSI** (1500 Mbps/lane, 80 MHz
  pixel clock), driven through `esp_lcd` (`panel_dsi.cpp`), not Arduino_GFX. The DPI panel
  owns its PSRAM framebuffer; LVGL flushes are DMA2D copies into it. The DSI PHY is powered
  by internal **LDO channel 3 at 2.5 V**, which must be enabled explicitly (otherwise:
  everything logs fine, screen stays black). `panel_dsi.cpp` replicates Espressif's
  `esp_lcd_jd9365` driver step for step (reset pulse, DPI panel **before** the init table,
  MADCTL/COLMOD/lane prologue, 197-command vendor table).
- **Backlight: LEDC PWM on GPIO 26, active low** (5 kHz, 10-bit, output inverted: the BSP's
  own setup).
- **Touch: GT911 family** on I2C (0x5D, fallback 0x14); reset and INT are **not connected**,
  so the driver probes both addresses and accepts any Goodix `9xxx` id (Skyglass found a
  GT9271 on the sibling 4C).
- **No PMIC, IMU or RTC** (they probe, find nothing, and the UI hides what they feed).
  **Audio (ES8311) is compiled out for now**: the I2S path is not verified on the P4.
- **The UI is scaled**: everything laid out for the 466 px AMOLED goes through `UI_S()`
  (`src/ui_scale.h`, ×1.717 here, identity on every other board) and fonts step up a tier
  (12→20, 14→24, 16→28, 28→48). The weather tile stays at its fetched 360 px, centred.
- **Built for ESP32-P4 silicon v3.x** (the bring-up board reads **v3.2**): pioarduino
  55.03.312 / core 3.3.12, whose prebuilt IDF 5.5 libs cover v3.01–v3.99 — the same
  platform as the 2.8C envs, plus a board JSON in `boards/`. Older v0.x–v1.x boards (like
  Skyglass's 4C) need the IDF 5.3 libs of platform 53.03.13 instead. The image header does
  not enforce either (it says v0.0–v655.35), so the wrong build flashes fine and then
  misbehaves: check yours first. The upload log says `Chip is ESP32-P4 (revision vX.Y)`.

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| LCD reset | 27 | | I2C SDA / SCL | 7 / 8 (100 kHz) |
| Backlight (active low) | 26 | | Touch INT / RST | not connected |
| DSI data | dedicated PHY (2 lanes) | | C6 SDIO CLK / CMD | 18 / 19 |
| I2S MCLK / BCLK / WS | 13 / 12 / 10 | | C6 SDIO D0–D3 | 14–17 |
| I2S DOUT / DIN | 9 / 11 | | C6 reset | 54 |
| Speaker amp enable | 53 | | | |

Sources, and they agree on every panel value: Waveshare's ESP-IDF BSP
`waveshare/esp32_p4_wifi6_touch_lcd_xc` 3.0.1 (the `800_800_3_4_INCH` branch, used by all
their IDF examples); their Arduino library (`examples/arduino/libraries/displays/
displays_config.h`, `SCREEN_3INCH_4_DSI`) in
[waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC);
and the working [EmbeddedWizardGUI 3.4C project](https://github.com/EmbeddedWizardGUI/ESP32-P4-WIFI6-Touch-LCD-3.4C).
The 197-command init table is identical in all three and is generated into
`src/panel_dsi_init_34c.h`. **It is not the 4C's table** (same length, register `0x40` differs),
so a 3.4C image on a 4C gives a wrong or blank picture. Board-level behaviour (SDIO link,
active-low backlight, touch family) comes from [Skyglass](https://github.com/SilentWolf75/skyglass)'s
hardware-tested port of the sibling 4C on the same XC PCB (`docs/PORT_P4.md` there).

### What to check on first boot
Serial log over the P4's native USB (the port you flash through; 115200). In order:
1. `board ESP32-P4-WIFI6-Touch-LCD-3.4C`. A reset loop or crash *before* this line on
   v0.x–v1.x silicon means the wrong IDF generation (see above).
2. `[dsi] JD9365 id 93 65 04`, `[dsi] init: 197 commands, 0 failed`, then `[dsi] up: 800x800`.
   A lit but black panel with NO `[dsi]` lines and an abort loop is the PHY clock source
   (see above); a lit black panel after them points at the init order or colour format,
   an unlit one at the backlight.
3. `[touch] Goodix GT9271 at 0x5D` (the bring-up board; `0x14` is the fallback address).
4. `[wifi] setup AP 'CapsuleRadar-Setup' up` on first boot, then connected after saving.

---

## Adding another board

1. Copy a header in `src/boards/`, fill in the pins **from the vendor demo or the Arduino
   core board variant** — never guess them.
2. Add an `[env:…]` in `platformio.ini` that `extends = amoled_common` and sets `-DBOARD_…`.
3. Set `BOARD_HAS_PMIC` / `BOARD_HAS_AUDIO` / `TOUCH_DRIVER_…` for what is actually fitted;
   add a touch driver alongside `touch_cst9217.cpp` / `touch_ft3168.cpp` if needed.
4. Confirm on hardware before committing: I2C scan, a full-panel test pattern for the gap,
   and a touch dot that lands under your fingertip.
5. Add the env to `.github/workflows/release.yml` so it ships with every release.

## Reference
- 1.75 wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75
- 1.43 wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.43
- 2.8C docs: https://docs.waveshare.com/ESP32-S3-LCD-2.8C
- Touch-LCD-2.8C wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8C
- P4 XC (3.4C/4C) docs: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-XC
- ESPHome device page (1.75 pin source): https://devices.esphome.io/devices/waveshare-esp32-s3-touch-amoled-175/
