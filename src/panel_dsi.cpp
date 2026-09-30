// MIPI-DSI panel bring-up for the ESP32-P4 boards (JD9365 controller).
//
// This replicates Espressif's esp_lcd_jd9365 driver — which Waveshare's BSP uses and
// the Arduino core does not ship — step for step, so the panel sees exactly what it
// sees under the vendor firmware:
//   1. hardware reset pulse (high 5 ms, low 10 ms, high 120 ms);
//   2. PHY LDO on, DSI bus, DBI command channel;
//   3. create the DPI video panel (it must exist BEFORE the init table is sent; sending
//      it first leaves a lit but blank panel — found the hard way on the 4C by Skyglass);
//   4. esp_lcd_jd9365's own prologue: user page, MADCTL, COLMOD (RGB565), lane count;
//   5. the vendor table, then esp_lcd_panel_init() on the DPI panel to start video.
#include "panel_dsi.h"
#include "config.h"

#if BOARD_PANEL_DSI
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_ldo_regulator.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_cache.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if defined(BOARD_P4_LCD_34C)
#include "panel_dsi_init_34c.h"
#define PANEL_INIT kPanelInit34c
#else
#error "BOARD_PANEL_DSI: no init table for this board"
#endif

// esp_lcd_jd9365 constants (esp_lcd_jd9365.c)
#define JD9365_CMD_PAGE     0xE0
#define JD9365_PAGE_USER    0x00
#define JD9365_CMD_DSI_INT0 0x80
#define JD9365_DSI_2_LANE   0x01
#define JD9365_COLMOD_565   0x55

static esp_lcd_panel_handle_t   s_panel = nullptr;
static esp_lcd_panel_io_handle_t s_io   = nullptr;
static SemaphoreHandle_t        s_copyDone = nullptr;

// DMA2D finished copying the caller's buffer into the framebuffer.
static bool IRAM_ATTR on_copy_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *) {
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_copyDone, &hpw);
    return hpw == pdTRUE;
}

static bool tx(uint8_t cmd, uint8_t val) {
    return esp_lcd_panel_io_tx_param(s_io, cmd, &val, 1) == ESP_OK;
}

esp_lcd_panel_handle_t panel_dsi_init() {
    // 1) reset (esp_lcd_jd9365 panel_jd9365_reset(), reset_active_high = 0)
    gpio_config_t rst = {};
    rst.mode = GPIO_MODE_OUTPUT;
    rst.pin_bit_mask = 1ULL << PIN_LCD_RST;
    gpio_config(&rst);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 1); delay(5);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 0); delay(10);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 1); delay(120);

    // 2) PHY power, DSI bus, DBI command channel
    static esp_ldo_channel_handle_t ldo = nullptr;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = DSI_LDO_CHANNEL;
    ldo_cfg.voltage_mv = DSI_LDO_MV;
    if (esp_ldo_acquire_channel(&ldo_cfg, &ldo) != ESP_OK) {
        Serial.println("[dsi] PHY LDO acquire failed");
        return nullptr;
    }
    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = DSI_LANES;
    // 0 = "driver picks the default for this silicon", exactly what Waveshare's BSP and
    // their Arduino DSI databus pass. Do NOT name MIPI_DSI_PHY_CLK_SRC_DEFAULT: in IDF 5.5
    // that is the legacy PLL_F20M reference, which P4 v3.x silicon no longer has, and
    // esp_lcd_new_dsi_bus() aborts in _mipi_dsi_ll_set_phy_pllref_clock_source() (seen on
    // a v3.2 3.4C: black panel, lit backlight, abort loop before setup() logs anything).
    bus_cfg.phy_clk_src = (decltype(bus_cfg.phy_clk_src))0;
    bus_cfg.lane_bit_rate_mbps = DSI_LANE_MBPS;
    if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) {
        Serial.println("[dsi] bus create failed");
        return nullptr;
    }
    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &s_io) != ESP_OK) {
        Serial.println("[dsi] DBI io create failed");
        return nullptr;
    }

    // 3) DPI video panel (BSP values; one framebuffer — LVGL renders partial buffers)
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = DSI_DPI_CLK_MHZ;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = SCREEN_W;
    dpi.video_timing.v_size = SCREEN_H;
    dpi.video_timing.hsync_pulse_width = DSI_HSYNC_PULSE;
    dpi.video_timing.hsync_back_porch  = DSI_HSYNC_BACK;
    dpi.video_timing.hsync_front_porch = DSI_HSYNC_FRONT;
    dpi.video_timing.vsync_pulse_width = DSI_VSYNC_PULSE;
    dpi.video_timing.vsync_back_porch  = DSI_VSYNC_BACK;
    dpi.video_timing.vsync_front_porch = DSI_VSYNC_FRONT;
    dpi.flags.use_dma2d = true;
    if (esp_lcd_new_panel_dpi(bus, &dpi, &s_panel) != ESP_OK) {
        Serial.println("[dsi] DPI panel create failed");
        return nullptr;
    }

    // 4) esp_lcd_jd9365 prologue. The ID read is diagnostic only (the driver logs it too).
    uint8_t id[3] = {0};
    if (esp_lcd_panel_io_rx_param(s_io, 0x04, id, 3) == ESP_OK)
        Serial.printf("[dsi] JD9365 id %02X %02X %02X\n", id[0], id[1], id[2]);
    bool ok = tx(JD9365_CMD_PAGE, JD9365_PAGE_USER) &&
              tx(LCD_CMD_MADCTL, 0x00) &&            // RGB element order
              tx(LCD_CMD_COLMOD, JD9365_COLMOD_565) &&
              tx(JD9365_CMD_DSI_INT0, JD9365_DSI_2_LANE);
    static_assert(DSI_LANES == 2, "the lane-count command above assumes 2 lanes");

    // 5) vendor table, checked write by write (a silent failure = a blank panel)
    int fails = 0;
    for (const PanelInitCmd &c : PANEL_INIT) {
        if (!tx(c.cmd, c.val) && fails++ < 3)
            Serial.printf("[dsi] init cmd 0x%02X failed\n", c.cmd);
        if (c.delay_ms) delay(c.delay_ms);
    }
    Serial.printf("[dsi] init: %u commands, %d failed%s\n",
                  (unsigned)(sizeof(PANEL_INIT) / sizeof(PANEL_INIT[0])), fails,
                  ok ? "" : " (prologue FAILED)");

    s_copyDone = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = on_copy_done;
    if (esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, nullptr) != ESP_OK) {
        vSemaphoreDelete(s_copyDone);            // wait_copy degrades to a no-op
        s_copyDone = nullptr;
    }
    if (esp_lcd_panel_init(s_panel) != ESP_OK) {
        Serial.println("[dsi] DPI panel init failed");
        return nullptr;
    }
    Serial.printf("[dsi] up: %dx%d, %d lanes @ %d Mbps, pclk %d MHz\n",
                  SCREEN_W, SCREEN_H, DSI_LANES, DSI_LANE_MBPS, DSI_DPI_CLK_MHZ);
    return s_panel;
}

void panel_dsi_draw(int x1, int y1, int x2, int y2, const void *pixels) {
    if (!s_panel) return;
    if (esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels) != ESP_OK) return;
    // Wait for DMA2D to finish reading `pixels`: LVGL (and the rotation scratch) reuse
    // the buffer as soon as this returns. Generous timeout: even a full 800x800 frame
    // (1.28 MB) is a few ms of DMA2D.
    if (s_copyDone) xSemaphoreTake(s_copyDone, pdMS_TO_TICKS(100));
}

void panel_dsi_fill_black() {
    void *fb = nullptr;
    if (!s_panel || esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK || !fb) return;
    const size_t n = (size_t)SCREEN_W * SCREEN_H * 2;
    memset(fb, 0, n);
    esp_cache_msync(fb, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);   // CPU writes -> PSRAM for the DPI DMA
}

void panel_dsi_backlight(uint8_t level) {
    static bool inited = false;
    if (!inited) {
        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_10_BIT;   // [BSP] 10-bit
        t.timer_num = LEDC_TIMER_1;
        t.freq_hz = 5000;                        // [BSP] LCD_LEDC backlight timer
        t.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&t);
        ledc_channel_config_t c = {};
        c.gpio_num = PIN_LCD_BL;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = LEDC_CHANNEL_1;
        c.timer_sel = LEDC_TIMER_1;
        c.duty = 0;
        c.flags.output_invert = LCD_BL_ACTIVE_LOW; // [BSP] output_invert = 1
        ledc_channel_config(&c);
        inited = true;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, ((uint32_t)level * 1023 + 127) / 255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

#endif  // BOARD_PANEL_DSI
