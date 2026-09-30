#pragma once
// ST7701 480x480 round IPS bring-up for the ESP32-S3-LCD-2.8C (BOARD_PANEL_ST7701 only).
// Requires tca9554_begin() first (LCD reset + SPI CS live on the expander).
#include <esp_lcd_panel_ops.h>

esp_lcd_panel_handle_t panel_st7701_init();   // nullptr on failure

// Block until the scanout finishes its current frame (bounce-buffer frame-finish event).
// Pass 0 to just drain a pending event. False on timeout / no event support.
bool panel_st7701_wait_frame(uint32_t timeout_ms);
