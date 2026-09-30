#pragma once
// JD9365 round panel over 2-lane MIPI-DSI (BOARD_PANEL_DSI only: the ESP32-P4 boards).
// The DPI panel owns its framebuffer in PSRAM; draw_bitmap copies into it with DMA2D.
#include <stdint.h>
#include <esp_lcd_panel_ops.h>

esp_lcd_panel_handle_t panel_dsi_init();   // LDO -> DSI bus -> DBI -> DPI -> init; nullptr on failure

// Copy a block into the framebuffer (end coordinates EXCLUSIVE, like esp_lcd). The copy
// runs on DMA2D; this waits for it, so the caller may reuse `pixels` on return (the
// rotation path refills one scratch buffer between consecutive draws).
void panel_dsi_draw(int x1, int y1, int x2, int y2, const void *pixels);

void panel_dsi_fill_black();               // clear the whole framebuffer

void panel_dsi_backlight(uint8_t level);   // 0 = off .. 255 = full (LEDC, active-low aware)
