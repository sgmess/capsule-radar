// ST7701 panel bring-up, ported from the Waveshare factory demo (ESP32-S3-LCD-2.8C-Demo.zip,
// Arduino/examples/LVGL_Arduino/Display_ST7701.cpp). The panel takes its init sequence over
// a 9-bit 3-wire SPI (the 9th bit is D/C, mapped to the SPI peripheral's 1 command bit +
// 8 address bits), with CS and reset on the TCA9554 expander. Pixels then stream over the
// 16-bit parallel RGB bus: no GRAM on the panel — esp_lcd continuously scans a full
// framebuffer out of PSRAM, with a small internal-RAM bounce buffer to keep the scanout
// stable while WiFi/flash traffic loads the PSRAM bus.
#include "panel_st7701.h"
#include "config.h"

#if BOARD_PANEL_ST7701
#include "tca9554.h"

#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Init sequence, verbatim from the demo: {cmd, nData, data...} records back to back.
// 0x11 (sleep out) is followed by the mandatory 120 ms pause in the player below.
static const uint8_t ST7701_INIT[] = {
    0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xEF,  1, 0x08,
    0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xC0,  2, 0x3B, 0x00,
    0xC1,  2, 0x10, 0x0C,
    0xC2,  2, 0x07, 0x0A,
    0xC7,  1, 0x00,
    0xCC,  1, 0x10,
    0xCD,  1, 0x08,
    0xB0, 16, 0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11,
    0xB1, 16, 0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F,
    0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x11,
    0xB0,  1, 0x5D,
    0xB1,  1, 0x3E,
    0xB2,  1, 0x81,
    0xB3,  1, 0x80,
    0xB5,  1, 0x4E,
    0xB7,  1, 0x85,
    0xB8,  1, 0x20,
    0xC1,  1, 0x78,
    0xC2,  1, 0x78,
    0xD0,  1, 0x88,
    0xE0,  3, 0x00, 0x00, 0x02,
    0xE1, 11, 0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33,
    0xE2, 12, 0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00,
    0xE3,  4, 0x00, 0x00, 0x11, 0x11,
    0xE4,  2, 0x44, 0x44,
    0xE5, 16, 0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0,
    0xE6,  4, 0x00, 0x00, 0x11, 0x11,
    0xE7,  2, 0x44, 0x44,
    0xE8, 16, 0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0,
    0xE9,  2, 0x36, 0x01,
    0xEB,  7, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40,
    0xED, 16, 0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF,
    0xEF,  6, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
    0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x00,
    0x11,  0,
    0x3A,  1, 0x66,
    0x36,  1, 0x00,
    0x35,  1, 0x00,
    0x29,  0,
};

// Frame-finish signal for display.cpp's double-buffer swap: in bounce-buffer mode the
// filler picks up a new front framebuffer when it wraps at a frame boundary, and
// on_bounce_frame_finish fires exactly there. (CONFIG_LCD_RGB_ISR_IRAM_SAFE is on, so
// the callback must be IRAM-resident and ISR-only APIs.)
static SemaphoreHandle_t s_frameSem = nullptr;
// Scanout desync self-heals: CONFIG_LCD_RGB_RESTART_IN_VSYNC=y (platformio.ini) makes
// the driver restart the LCD DMA in the VSYNC handler after an underrun, so a starved
// bounce buffer costs one glitched frame instead of a permanently shifted panel.

static bool IRAM_ATTR on_frame_done_cb(esp_lcd_panel_handle_t,
                                       const esp_lcd_rgb_panel_event_data_t *, void *) {
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_frameSem, &hpw);
    return hpw == pdTRUE;
}

bool panel_st7701_wait_frame(uint32_t timeout_ms) {
    if (!s_frameSem) return false;
    return xSemaphoreTake(s_frameSem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void spi_write9(spi_device_handle_t spi, bool isData, uint8_t byte) {
    spi_transaction_t t = {};
    t.cmd  = isData ? 1 : 0;   // D/C bit (device configured with 1 command bit)
    t.addr = byte;             // the payload byte rides in the 8 address bits
    spi_device_transmit(spi, &t);
}

esp_lcd_panel_handle_t panel_st7701_init() {
    if (!tca9554_ok()) return nullptr;

    // hardware reset via the expander
    tca9554_set(EXIO_LCD_RST, false);
    delay(10);
    tca9554_set(EXIO_LCD_RST, true);
    delay(50);

    // 3-wire SPI for the init sequence (bus stays claimed afterwards, as in the demo;
    // GPIO 1/2 have no other role on this board)
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_LCD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 64;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return nullptr;
    spi_device_interface_config_t dev = {};
    dev.command_bits = 1;
    dev.address_bits = 8;
    dev.mode = SPI_MODE0;
    dev.clock_speed_hz = 40000000;
    dev.spics_io_num = -1;         // CS is on the expander
    dev.queue_size = 1;
    spi_device_handle_t spi = nullptr;
    if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return nullptr;

    tca9554_set(EXIO_LCD_CS, false);
    delay(10);
    for (size_t i = 0; i < sizeof(ST7701_INIT); ) {
        const uint8_t cmd = ST7701_INIT[i++];
        const uint8_t n   = ST7701_INIT[i++];
        spi_write9(spi, false, cmd);
        for (uint8_t d = 0; d < n; ++d) spi_write9(spi, true, ST7701_INIT[i++]);
        if (cmd == 0x11) delay(120);   // sleep-out settle time
    }
    tca9554_set(EXIO_LCD_CS, true);
    delay(10);

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_PLL240M;
    cfg.timings.pclk_hz = RGB_PCLK_HZ;
    cfg.timings.h_res = SCREEN_W;
    cfg.timings.v_res = SCREEN_H;
    cfg.timings.hsync_pulse_width = RGB_HSYNC_PW;
    cfg.timings.hsync_back_porch  = RGB_HSYNC_BP;
    cfg.timings.hsync_front_porch = RGB_HSYNC_FP;
    cfg.timings.vsync_pulse_width = RGB_VSYNC_PW;
    cfg.timings.vsync_back_porch  = RGB_VSYNC_BP;
    cfg.timings.vsync_front_porch = RGB_VSYNC_FP;
    cfg.timings.flags.pclk_active_neg = 0;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 2;                             // double-buffered scanout: display.cpp draws into
                                                 // the back FB and swaps at frame boundaries (no tearing)
    cfg.bounce_buffer_size_px = 30 * SCREEN_W;   // 30 lines: proven shift-free at 18 MHz (the shift is
                                                 // bounce underrun; the compositor raised PSRAM traffic).
                                                 // The 57.6 KB internal cost is paid for by moving the
                                                 // LVGL draw buffer to PSRAM (display.cpp) — that freed
                                                 // the contiguous internal RAM mbedTLS needs.
                                                 // NOTE size_px is PER buffer and esp_lcd allocates TWO, so
                                                 // 20 lines = 38.4 KB internal RAM. 30 lines (v1.7.2/3) cost
                                                 // +19.2 KB and starved mbedTLS -> every feed handshake failed
                                                 // (no aircraft, and no coastline either: it only projects on
                                                 // the first successful poll). Deeper bounce is also pointless
                                                 // now: the >18 MHz shift is ST7701 line-sync, not underrun.
    cfg.psram_trans_align = 64;
    cfg.hsync_gpio_num = PIN_RGB_HSYNC;
    cfg.vsync_gpio_num = PIN_RGB_VSYNC;
    cfg.de_gpio_num = PIN_RGB_DE;
    cfg.pclk_gpio_num = PIN_RGB_PCLK;
    cfg.disp_gpio_num = -1;
    const int dataPins[16] = { PIN_RGB_D0, PIN_RGB_D1, PIN_RGB_D2,  PIN_RGB_D3,
                               PIN_RGB_D4, PIN_RGB_D5, PIN_RGB_D6,  PIN_RGB_D7,
                               PIN_RGB_D8, PIN_RGB_D9, PIN_RGB_D10, PIN_RGB_D11,
                               PIN_RGB_D12, PIN_RGB_D13, PIN_RGB_D14, PIN_RGB_D15 };
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = dataPins[i];
    cfg.flags.fb_in_psram = 1;

    esp_lcd_panel_handle_t panel = nullptr;
    if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return nullptr;

    s_frameSem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_bounce_frame_finish = on_frame_done_cb;
    if (esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr) != ESP_OK) {
        vSemaphoreDelete(s_frameSem);            // wait_frame degrades to a no-op
        s_frameSem = nullptr;
    }

    if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) return nullptr;
    return panel;
}

#endif  // BOARD_PANEL_ST7701
