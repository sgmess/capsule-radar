#pragma once
// Panel + LVGL glue. Owns the panel + LVGL display driver so main.cpp stays glue-only.
// Backends: CO5300 AMOLED via Arduino_GFX/QSPI (default) or ST7701 IPS via esp_lcd
// parallel RGB (BOARD_PANEL_ST7701, the 2.8C). See display.cpp.
#include <stdint.h>

namespace display {

// Init the panel and LVGL, then build the UI (ui_create()).
// Returns false if the panel failed to initialize.
bool begin();

// Pump LVGL: render dirty areas + run LVGL timers. Call every loop() iteration.
void loop();

// 0..255 brightness (AMOLED: panel command 0x51; 2.8C: backlight PWM).
void setBrightness(uint8_t v);

// ms since the last touch (LVGL inactivity timer) — for idle auto-dim.
uint32_t inactiveMs();

// 2.8C sweep compositor: the radar sweep is blended into the scanout framebuffer AFTER
// LVGL, so the 30 ms sweep tick no longer forces LVGL to repaint every layer under the
// wedge. radar_view feeds the colors/enable state; angles come from the wall clock
// inside display.cpp. No-ops on the AMOLED builds (LVGL draws the fan there).
void sweepConfig(uint16_t ring565, uint16_t lead565, bool enabled);
void sweepSetVisible(bool onRadarView);   // ui.cpp: only the radar tile shows the sweep
bool sweepCompositor();                   // true when the compositor is active on this build/boot

// Rotate the whole UI clockwise by an arbitrary number of degrees (normalized to
// 0..359), e.g. to compensate for any enclosure/mounting angle. Cardinal rotations
// keep their optimized flush paths; other angles use a PSRAM framebuffer. Touch input
// is transformed back into the same logical coordinate space.
void setRotation(uint16_t degrees);
uint16_t rotation();

} // namespace display

uint32_t display_frames();   // total rendered frames (for FPS measurement)
