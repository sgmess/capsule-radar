#pragma once
// Capsule Radar — build & user configuration.

#define FW_VERSION "1.4.5"   // shown on the web config page + Stats screen; bump on release

// ---------- Board ----------
// The pin map, panel, touch driver and which peripherals exist all live in a
// per-board header. Select one with a build flag in platformio.ini; the 1.75 is the
// default so an unflagged build behaves exactly as before.
//   -DBOARD_AMOLED_143  -> Waveshare ESP32-S3-Touch-AMOLED-1.43
//   -DBOARD_LCD_28C     -> Waveshare ESP32-S3-LCD-2.8C (non-touch, ST7701 RGB)
//   -DBOARD_TOUCH_LCD_28C -> Waveshare ESP32-S3-Touch-LCD-2.8C (same + GT911 touch)
//   -DBOARD_P4_LCD_34C  -> Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C (ESP32-P4, MIPI-DSI)
//   (none)              -> Waveshare ESP32-S3-Touch-AMOLED-1.75  (reference board)
// Never guess pins for a new board: take them from the vendor demo or the Arduino
// core board variant, then confirm them on hardware before committing.
// Included first so a board can override the screen defaults below.
#if defined(BOARD_AMOLED_143)
#  include "boards/board_amoled_143.h"
#elif defined(BOARD_P4_LCD_34C)
#  include "boards/board_p4_lcd_34c.h"
#elif defined(BOARD_TOUCH_LCD_28C)
#  include "boards/board_touch_lcd_28c.h"
#elif defined(BOARD_LCD_28C)
#  include "boards/board_lcd_28c.h"
#else
#  include "boards/board_amoled_175.h"
#endif
// Defaults for capabilities the AMOLED headers predate.
#ifndef BOARD_PANEL_ST7701
#define BOARD_PANEL_ST7701  0              // 0 = CO5300 QSPI AMOLED via Arduino_GFX
#endif
#ifndef BOARD_PANEL_DSI
#define BOARD_PANEL_DSI     0              // 1 = MIPI-DSI panel via esp_lcd (the P4 boards)
#endif
#ifndef BOARD_HAS_WIFIMANAGER
#define BOARD_HAS_WIFIMANAGER 1            // 0 = no native PHY (P4): wifi_setup.cpp's AP instead
#endif
#ifndef UI_SCALE_PERMILLE
#define UI_SCALE_PERMILLE   1000           // UI_S() factor for hand-placed UI geometry (466 design)
#endif
#ifndef UI_BIG_PANEL
#define UI_BIG_PANEL        0              // 1 = step fonts up a tier on big panels
#endif
#ifndef I2C_CLOCK_HZ
#define I2C_CLOCK_HZ        400000
#endif
#ifndef BOARD_HAS_TOUCH
#define BOARD_HAS_TOUCH     1
#endif
#ifndef BOARD_HAS_BUZZER
#define BOARD_HAS_BUZZER    0
#endif
#ifndef BOARD_HAS_BATT_ADC
#define BOARD_HAS_BATT_ADC  0
#endif

// ---------- Home location (default: Dénia, Spain) ----------
// Overridable at runtime via the captive portal (stored in NVS).
#define HOME_LAT_DEFAULT   38.8409
#define HOME_LON_DEFAULT    0.1059

// ---------- Radar ----------
#define RANGE_KM_DEFAULT    30.0f          // display range (outer ring). Query is wider, see below.
// Feed query radius = display range × MULT, clamped to [MIN, MAX]. Querying a bit wider than
// the display shows off-range traffic as edge arrows. The floor MUST stay small: the old 50 km
// floor made small display ranges still pull a huge aircraft list in busy airspace, which timed
// out the poll (feed permanently amber near big hubs). Fix contributed by @alexzogh (STLWarehouse).
#define ADSB_QUERY_MULT     1.4f
#define ADSB_QUERY_MIN_KM   12.0f
#define ADSB_QUERY_MAX_KM   150.0f
static const float RANGE_STEPS_KM[] = {10.0f, 20.0f, 30.0f, 50.0f, 100.0f};
#define POLL_INTERVAL_MS    2000           // be gentle with the free API (>=1000)
#define POLL_INTERVAL_BATTERY_MS 5000      // slower polling when running on battery
#define MOTION_INTERP       1              // 1 = glyphs glide between polls; 0 = snap to new pos
#define AC_STALE_MS         15000          // keep the last contacts through brief empty feed responses

// ---------- Weather forecast (Open-Meteo, no API key) ----------
#define WEATHER_REFRESH_MS  1800000UL      // 30 minutes; forecast data changes slowly
#define WX_RADAR_REFRESH_MS 300000UL       // RainViewer frames update about every 5 minutes
#define CLOUD_IMAGE_REFRESH_MS 600000UL    // EUMETSAT MTG cloud imagery; cache for 10 minutes

// ---------- Screen ----------
// 466x466 CO5300 AMOLED unless the board header overrides it (the 2.8C is 480x480).
#ifndef SCREEN_W
#define SCREEN_W            466
#define SCREEN_H            466
#define SCREEN_CX           233
#define SCREEN_CY           233
#define RADAR_R_OUTER_PX    218            // outer ring radius in pixels
#endif
#define LV_COLOR_DEPTH_BITS 16
// LCD_COL_OFFSET / LCD_ROW_OFFSET / LCD_QSPI_HZ are panel-specific -> board header.
#define BRIGHTNESS_DEFAULT  200            // 0..255 (AMOLED: panel cmd 0x51; LCD: backlight PWM)
#define TZ_STR              "CET-1CEST,M3.5.0,M10.5.0/3"  // POSIX TZ (Spain) for local time/date
#define BRIGHTNESS_IDLE     25             // dimmed after no touch for IDLE_DIM_MS
#ifndef IDLE_DIM_MS
#define IDLE_DIM_MS         20000          // dim the screen after this long without a touch
#endif

// ---------- Radar sweep ----------
// Shared by radar_view.cpp (LVGL fan on the AMOLEDs + sim) and display.cpp (the 2.8C's
// post-LVGL sweep compositor).
#define SWEEP_PERIOD_MS     8000           // one full revolution
#define SWEEP_FRAME_MS      30             // sweep animation cadence
#define SWEEP_TRAIL_DEG     38.0f          // fading trail arc behind the lead line
#define SWEEP_TRAIL_OPA     72             // trail opacity at the lead edge (0..255)
#define SWEEP_LEAD_OPA      217            // bright leading-edge line opacity

// ---------- ADS-B API (free, non-commercial) ----------
// Providers are tried in this order; each is paced independently (see AdsbClient).
// All three return the same readsb shape (an "ac" array), but NOT the same URL path,
// so each carries its own template in the provider table in adsb_client.cpp.
#define ADSB_PRIMARY_HOST   "api.airplanes.live"   // GET /v2/point/{lat}/{lon}/{radius_nm}
                                                    //   now requires prior approval by email
#define ADSB_OPENDATA_HOST  "opendata.adsb.fi"     // GET /api/v3/lat/{lat}/lon/{lon}/dist/{nm}
                                                    //   documented 1 req/s; personal use only,
                                                    //   attribution required (see docs/DATA_SOURCE.md)
#define ADSB_FALLBACK_HOST  "api.adsb.lol"          // same readsb format; limits are dynamic
#define ADSB_PROVIDER_COUNT 3
// Sent with setUserAgent(), never addHeader() — see the note in docs/DATA_SOURCE.md.
// Carries FW_VERSION: now that the header actually goes out, and adsb.lol checks it for
// valid contact info, a provider should be able to tell which build is talking to them.
#define ADSB_USER_AGENT     "CapsuleRadar/" FW_VERSION " (ESP32-S3 hobby; +https://github.com/socquique/capsule-radar)"
#define ADSB_HTTPS_INSECURE 1               // 1 = setInsecure() (hobby). 0 = use pinned root CA.
#define ADSB_MAX_AIRCRAFT   60              // hard cap parsed per poll (protect RAM in busy areas)
// How long to stop asking a provider that refused us. 403 is a policy refusal (needs
// approval, or a User-Agent they reject) and will not clear in seconds; 429 just means
// we were too fast. Without these, a permanently-403 provider is retried every poll,
// which doubles the request rate onto the surviving provider and trips ITS rate limit.
#define ADSB_COOLDOWN_403_MS  900000UL      // first park after a policy refusal (15 min)
#define ADSB_COOLDOWN_403_MAX_MS 21600000UL // cap for the escalating 403 park (6 h) — see AdsbPacer::onRefused
#define ADSB_SPACING_STEP_MS    4000UL      // first extra gap imposed after a 429
#define ADSB_SPACING_MAX_MS    60000UL      // never space a provider out further than this
#define ADSB_SPACING_EASE_OKS       3       // successes needed before easing the gap back down
#define ADSB_FEED_STALE_MS     60000UL      // no successful fetch for this long -> HUD warning

// ---------- Debug ----------
#define DEBUG_MEM           0               // 1 = print a [mem] heap/fps line every 5s on serial

// Safety net: catches a board header that still has placeholder pins in it.
#if (PIN_LCD_SCLK < 0) || (PIN_I2C_SDA < 0)
#  error "board header: QSPI/I2C pins are placeholders (-1). Fill in the real values."
#endif
