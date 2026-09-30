// Panel + LVGL glue. Two panel backends behind the same display.h API, picked by the
// board header selected in config.h (see src/boards/):
//  - CO5300 466x466 AMOLED via Arduino_GFX (QSPI), the default: windowed flushes into
//    the panel's GRAM. On boards with an AXP2101 the panel runs off the always-on DC1
//    rail, so it lights up without configuring the PMIC first.
//    NOTE: gfx->begin() must stay ahead of touch_begin() — on the 1.43 the touch
//    controller's reset is tied to the panel reset and it is absent from I2C until then.
//  - BOARD_PANEL_ST7701 (the 2.8C): ST7701 480x480 IPS via esp_lcd's parallel-RGB
//    peripheral. The panel has no GRAM; the S3 scans a PSRAM framebuffer out
//    continuously, and a post-LVGL compositor draws the radar sweep (see below).
// Everything else (LVGL plumbing, the rotation engine, flush paths) is shared.
// The actual UI is built by ui_create() (shared with the native SDL sim).
#include "display.h"
#include "config.h"
#include "radar_view.h"
#include "ui.h"
#include "touch.h"

#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#if BOARD_PANEL_ST7701
#include "panel_st7701.h"
#include "tca9554.h"
#include <esp_lcd_panel_rgb.h>
static esp_lcd_panel_handle_t s_panel = nullptr;
// Double-buffered scanout (no tearing at any pixel clock): frames are written into the
// back framebuffer only, then the scanout pointer is swapped at a frame boundary.
// Cache coherency is free here: in bounce-buffer mode the scanout data is memcpy'd from
// the framebuffer by the CPU (through the cache), not read raw by DMA. (A GDMA
// async-memcpy flush was tried in @ijord's port and reverted for exactly that reason:
// raw DMA writes to PSRAM are invisible to the cached bounce reads.)
static uint16_t *s_fb[2] = { nullptr, nullptr };  // the panel driver's PSRAM framebuffers
static int  s_backIdx = 1;              // index being drawn into (driver scans out fb 0 first)
static bool s_swapPending = false;      // swap requested; not yet picked up by the scanout

// --- sweep compositor --------------------------------------------------------
// LVGL renders into s_clean (the canonical scope image, physical orientation); the
// compositor then assembles scanout frames = s_clean + the sweep wedge, swapped at
// vsync. LVGL therefore only re-renders when DATA changes — the 30 ms sweep tick
// costs a small blend, not a full layer-stack repaint (~90 ms per frame at 480x480).
// The wedge is angular per pixel: s_angMap holds each pixel's bearing (15-bit, bit15 =
// outside the scope disc), the fade comes from a LUT on (sweepAngle - pixelAngle),
// ordered-dithered at the RGB565 quantize so the fade has no banding.
struct Rect { int x1, y1, x2, y2; bool valid; };
static uint16_t *s_clean  = nullptr;    // LVGL's composed output (PSRAM, full frame)
static uint16_t *s_angMap = nullptr;    // per-pixel bearing map (PSRAM, full frame)
static Rect      s_pend[2]      = {};   // LVGL-dirty regions each fb hasn't received yet
static Rect      s_lastWedge[2] = {};   // where each fb last had a wedge blended
static uint16_t  s_swRing565 = 0x07E0, s_swLead565 = 0x9FF3;  // tint colors (set by radar_view)
static bool      s_swEnabled = false;
static bool      s_swVisible = true;    // radar tile active (ui.cpp drives this)
static uint32_t  s_lastComposeMs = 0;
#define SWEEP_ANG_UNITS   32768                                  // 360 deg in map units
#define SWEEP_TRAIL_U     ((int)(SWEEP_TRAIL_DEG / 360.0f * SWEEP_ANG_UNITS))
#define SWEEP_LUT_SHIFT   3
#define SWEEP_LUT_N       ((SWEEP_TRAIL_U >> SWEEP_LUT_SHIFT) + 2)
static uint8_t   s_fadeLut[SWEEP_LUT_N];

static inline void rect_grow(Rect &r, int x1, int y1, int x2, int y2) {
    if (!r.valid) { r = { x1, y1, x2, y2, true }; return; }
    if (x1 < r.x1) r.x1 = x1;
    if (y1 < r.y1) r.y1 = y1;
    if (x2 > r.x2) r.x2 = x2;
    if (y2 > r.y2) r.y2 = y2;
}
#else
#include <Arduino_GFX_Library.h>
// --- Arduino_GFX panel -------------------------------------------------------
// Typed as Arduino_CO5300* (not Arduino_GFX*) so setBrightness() — declared on
// Arduino_OLED, not the GFX base — is reachable.
static Arduino_DataBus *s_bus = nullptr;
static Arduino_CO5300  *s_gfx = nullptr;
#endif

// --- LVGL plumbing -----------------------------------------------------------
// Partial draw-buffer height, kept in fast internal RAM. On the 2.8C three constraints
// meet here:
//   - the buffer must be INTERNAL: LVGL rendering from PSRAM adds read traffic to the
//     same bus the RGB scanout lives on -> bounce underruns (image shift + noise);
//   - internal RAM must stay contiguous enough for mbedTLS (2x ~16.7 KB per handshake)
//     next to the RGB driver's internal bounce buffers;
//   - the sweep compositor makes LVGL renders rare and mostly small, so a SMALL buffer
//     costs almost nothing there. 12 lines (11.5 KB) satisfies all three.
#if BOARD_PANEL_ST7701
#define LVGL_BUF_LINES 12
#else
#define LVGL_BUF_LINES 40    // the AMOLEDs render every sweep frame through LVGL: keep it roomy
#endif
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;
static lv_color_t        *s_buf1 = nullptr;
static lv_color_t        *s_buf2 = nullptr;

static volatile uint32_t s_frameCount = 0;   // rendered frames (last-flush), for FPS measurement
uint32_t display_frames() { return s_frameCount; }

static volatile uint16_t s_rot = 0;          // clockwise display rotation, 0..359 degrees
static float      s_rotCos = 1.0f;
static float      s_rotSin = 0.0f;
static int32_t    s_rotCosQ16 = 65536;       // fixed-point values used in the per-pixel hot path
static int32_t    s_rotSinQ16 = 0;
static lv_color_t *s_rotBuf = nullptr;       // PSRAM scratch for rotated output (see begin())
static lv_color_t *s_frameBuf = nullptr;     // full logical framebuffer for arbitrary-angle sampling

static void draw_block(int16_t x, int16_t y, lv_color_t *pixels, uint16_t w, uint16_t h) {
#if BOARD_PANEL_ST7701
    if (!s_fb[1] || !s_clean) {
        // fallback (missing 2nd FB or clean buffer): straight into the scanout FB.
        // End coordinates are exclusive; esp_lcd memcpys the block into the framebuffer.
        esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels);
        return;
    }
    // LVGL content goes into the CLEAN buffer only; the compositor moves it (plus the
    // sweep) into the scanout framebuffers. Both framebuffers still need this region.
    const uint16_t *src = (const uint16_t *)pixels;   // lv_color_t is RGB565, swap 0
    uint16_t *dst = s_clean + (int)y * SCREEN_W + x;
    for (int row = 0; row < h; ++row, dst += SCREEN_W)
        memcpy(dst, src + row * w, (size_t)w * sizeof(uint16_t));
    rect_grow(s_pend[0], x, y, x + w - 1, y + h - 1);
    rect_grow(s_pend[1], x, y, x + w - 1, y + h - 1);
#elif (LV_COLOR_16_SWAP != 0)
    s_gfx->draw16bitBeRGBBitmap(x, y, (uint16_t *)pixels, w, h);
#else
    s_gfx->draw16bitRGBBitmap(x, y, (uint16_t *)pixels, w, h);
#endif
}

// Blank the physical screen (used at init and when a rotation change uncovers pixels).
static void panel_fill_black() {
#if BOARD_PANEL_ST7701
    for (int i = 0; i < 2; ++i)
        if (s_fb[i]) memset(s_fb[i], 0, (size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t));
    if (s_clean) memset(s_clean, 0, (size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t));
    s_pend[0] = s_pend[1] = { 0, 0, SCREEN_W - 1, SCREEN_H - 1, true };
    s_lastWedge[0].valid = s_lastWedge[1].valid = false;
#else
    if (s_gfx) s_gfx->fillScreen(RGB565_BLACK);
#endif
}

// Last flush of an LVGL frame. On the 2.8C the compositor owns swapping and frame
// counting (compose_step below); LVGL frames just land in s_clean.
static void frame_done() {
#if !BOARD_PANEL_ST7701
    s_frameCount++;
#endif
}

#if BOARD_PANEL_ST7701
// --- sweep compositor internals ----------------------------------------------
// One-time tables: per-pixel bearing map + squared-radius clip, and the trail fade LUT.
static void sweep_build_tables() {
    const size_t n = (size_t)SCREEN_W * SCREEN_H;
    s_angMap = (uint16_t *)heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_angMap) return;
    const float rMax2 = (float)(RADAR_R_OUTER_PX + 2) * (RADAR_R_OUTER_PX + 2);
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            const float dx = (float)(x - SCREEN_CX);
            const float dy = (float)(y - SCREEN_CY);
            uint16_t v;
            if (dx * dx + dy * dy > rMax2) {
                v = 0x8000;                                   // outside the scope disc
            } else {
                float a = atan2f(dx, -dy);                     // 0 = north, clockwise (rim_point convention)
                if (a < 0) a += 2.0f * (float)M_PI;
                int u = (int)(a * ((float)SWEEP_ANG_UNITS / (2.0f * (float)M_PI)));
                if (u >= SWEEP_ANG_UNITS) u = SWEEP_ANG_UNITS - 1;
                v = (uint16_t)u;
            }
            s_angMap[(size_t)y * SCREEN_W + x] = v;
        }
    }
    for (int i = 0; i < SWEEP_LUT_N; ++i) {
        const float delta = (float)((i << SWEEP_LUT_SHIFT) + (1 << (SWEEP_LUT_SHIFT - 1)));
        float frac = 1.0f - delta / (float)SWEEP_TRAIL_U;      // 1 at the lead edge -> 0 at the tail
        if (frac < 0) frac = 0;
        s_fadeLut[i] = (uint8_t)(frac * frac * (float)SWEEP_TRAIL_OPA);
    }
}

// Alpha-blend `tint` over dst at `opa`, ordered-dithered into RGB565 so the trail fade
// has no visible banding (565 alone gives only 32/64 levels per channel).
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline uint16_t blend565_dither(uint16_t dst, uint16_t tint, uint32_t opa, uint8_t d) {
    const uint32_t ia = 255 - opa;
    uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    uint32_t tr = (tint >> 11) & 0x1F, tg = (tint >> 5) & 0x3F, tb = tint & 0x1F;
    // widen to 8 bits, blend, then dither-quantize back down
    dr = (dr << 3) | (dr >> 2); dg = (dg << 2) | (dg >> 4); db = (db << 3) | (db >> 2);
    tr = (tr << 3) | (tr >> 2); tg = (tg << 2) | (tg >> 4); tb = (tb << 3) | (tb >> 2);
    uint32_t r = (tr * opa + dr * ia + 127) / 255;
    uint32_t g = (tg * opa + dg * ia + 127) / 255;
    uint32_t b = (tb * opa + db * ia + 127) / 255;
    r += d >> 1; g += d >> 2; b += d >> 1;                     // half-LSB ordered dither
    r >>= 3; g >>= 2; b >>= 3;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Bounding box of the wedge (lead line + trail) at `angleU`, padded, screen-clamped.
static Rect wedge_bbox_u(int angleU) {
    const float toRad = 2.0f * (float)M_PI / (float)SWEEP_ANG_UNITS;
    float minX = SCREEN_CX, maxX = SCREEN_CX, minY = SCREEN_CY, maxY = SCREEN_CY;
    for (int i = 0; i <= 10; ++i) {
        const float a = (float)(angleU - SWEEP_TRAIL_U * i / 10) * toRad;
        const float x = SCREEN_CX + (RADAR_R_OUTER_PX + 2) * sinf(a);
        const float y = SCREEN_CY - (RADAR_R_OUTER_PX + 2) * cosf(a);
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    Rect r = { (int)minX - 3, (int)minY - 3, (int)maxX + 3, (int)maxY + 3, true };
    if (r.x1 < 0) r.x1 = 0;
    if (r.y1 < 0) r.y1 = 0;
    if (r.x2 >= SCREEN_W) r.x2 = SCREEN_W - 1;
    if (r.y2 >= SCREEN_H) r.y2 = SCREEN_H - 1;
    return r;
}

// Assemble the next scanout frame into the back framebuffer: refresh the regions this
// buffer is missing from s_clean, blend the wedge, plot the lead line, swap at vsync.
static void compose_step() {
    if (!s_fb[1] || !s_clean || !s_angMap) return;
    const uint32_t now = millis();
    const bool swOn = s_swEnabled && s_swVisible;
    const bool sweepDue = swOn && (now - s_lastComposeMs >= SWEEP_FRAME_MS);
    Rect &pend = s_pend[s_backIdx];
    Rect &oldW = s_lastWedge[s_backIdx];
    if (!pend.valid && !sweepDue && !(oldW.valid && !swOn)) return;

    if (s_swapPending) {                       // previous swap not yet picked up by scanout?
        if (!panel_st7701_wait_frame(0)) return;   // non-blocking: try again next loop pass
        s_swapPending = false;
    }

    // physical sweep angle = wall-clock angle + display rotation
    const int angleU = (int)(((uint64_t)(now % SWEEP_PERIOD_MS) * SWEEP_ANG_UNITS) / SWEEP_PERIOD_MS
                             + ((uint32_t)s_rot * SWEEP_ANG_UNITS) / 360) % SWEEP_ANG_UNITS;
    Rect newW = swOn ? wedge_bbox_u(angleU) : Rect{ 0, 0, 0, 0, false };

    Rect reg = { 0, 0, 0, 0, false };          // everything this buffer must receive
    if (pend.valid) rect_grow(reg, pend.x1, pend.y1, pend.x2, pend.y2);
    if (oldW.valid) rect_grow(reg, oldW.x1, oldW.y1, oldW.x2, oldW.y2);
    if (newW.valid) rect_grow(reg, newW.x1, newW.y1, newW.x2, newW.y2);
    if (!reg.valid) return;

    uint16_t *fb = s_fb[s_backIdx];
    const size_t rowBytes = (size_t)(reg.x2 - reg.x1 + 1) * sizeof(uint16_t);
    for (int y = reg.y1; y <= reg.y2; ++y)
        memcpy(fb + (size_t)y * SCREEN_W + reg.x1,
               s_clean + (size_t)y * SCREEN_W + reg.x1, rowBytes);

    if (newW.valid) {
        // trail: per-pixel angular fade behind the lead (continuous, dithered)
        for (int y = newW.y1; y <= newW.y2; ++y) {
            const uint16_t *am = s_angMap + (size_t)y * SCREEN_W;
            uint16_t *row = fb + (size_t)y * SCREEN_W;
            const uint8_t *bay = &BAYER4[(y & 3) << 2];
            for (int x = newW.x1; x <= newW.x2; ++x) {
                const uint16_t m = am[x];
                if (m & 0x8000) continue;                        // outside the scope disc
                const int delta = (angleU - (int)m) & (SWEEP_ANG_UNITS - 1);
                if (delta >= SWEEP_TRAIL_U) continue;
                const uint8_t opa = s_fadeLut[delta >> SWEEP_LUT_SHIFT];
                if (opa < 2) continue;
                row[x] = blend565_dither(row[x], s_swRing565, opa, bay[x & 3]);
            }
        }
        // lead line: a 2px bright ray from hub to rim at the sweep angle
        const float a = (float)angleU * (2.0f * (float)M_PI / (float)SWEEP_ANG_UNITS);
        const float sx = sinf(a), cy = cosf(a);
        for (int r = 0; r <= RADAR_R_OUTER_PX; ++r) {
            const int px = SCREEN_CX + (int)lroundf(sx * r);
            const int py = SCREEN_CY - (int)lroundf(cy * r);
            for (int oy = 0; oy <= 1; ++oy)
                for (int ox = 0; ox <= 1; ++ox) {
                    const int xx = px + ox, yy = py + oy;
                    if (xx < 0 || xx >= SCREEN_W || yy < 0 || yy >= SCREEN_H) continue;
                    uint16_t *p = fb + (size_t)yy * SCREEN_W + xx;
                    *p = blend565_dither(*p, s_swLead565, SWEEP_LEAD_OPA,
                                         BAYER4[((yy & 3) << 2) | (xx & 3)]);
                }
        }
    }

    panel_st7701_wait_frame(0);                // drain any stale frame-finish event
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, SCREEN_W, SCREEN_H, fb);   // zero-copy swap
    s_swapPending = true;
    oldW = newW;
    pend.valid = false;
    s_backIdx = 1 - s_backIdx;
    s_lastComposeMs = now;
    s_frameCount++;                            // FPS counts composed scanout frames
}
#endif  // BOARD_PANEL_ST7701

// Render the physical bounding box affected by a logical dirty rectangle. Sampling
// from the full logical framebuffer avoids gaps/overdraw artifacts that forward-mapping
// individual source pixels would create at non-cardinal angles.
static void flush_arbitrary(const lv_area_t *area) {
    const float cx = (SCREEN_W - 1) * 0.5f;
    const float cy = (SCREEN_H - 1) * 0.5f;
    const float xs[4] = {(float)area->x1, (float)area->x2, (float)area->x2, (float)area->x1};
    const float ys[4] = {(float)area->y1, (float)area->y1, (float)area->y2, (float)area->y2};
    float minX = (float)SCREEN_W, minY = (float)SCREEN_H, maxX = -1.0f, maxY = -1.0f;
    for (int i = 0; i < 4; ++i) {
        const float rx = xs[i] - cx;
        const float ry = ys[i] - cy;
        const float dx = cx + s_rotCos * rx - s_rotSin * ry;
        const float dy = cy + s_rotSin * rx + s_rotCos * ry;
        if (dx < minX) minX = dx;
        if (dx > maxX) maxX = dx;
        if (dy < minY) minY = dy;
        if (dy > maxY) maxY = dy;
    }

    // Include nearest-neighbour edge pixels and preserve the panel's required 2-pixel
    // alignment (even start, odd end) in both axes.
    int x1 = (int)floorf(minX) - 1;
    int y1 = (int)floorf(minY) - 1;
    int x2 = (int)ceilf(maxX) + 1;
    int y2 = (int)ceilf(maxY) + 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    x1 &= ~1;
    y1 &= ~1;
    x2 |= 1;
    y2 |= 1;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    if (x1 > x2 || y1 > y2) return;

    const int outW = x2 - x1 + 1;
    const lv_color_t black = lv_color_black();
    const int64_t centerX2Q16 = (int64_t)(SCREEN_W - 1) * 65536;
    const int64_t centerY2Q16 = (int64_t)(SCREEN_H - 1) * 65536;
    const int32_t stepX = s_rotCosQ16 * 2;
    const int32_t stepY = -s_rotSinQ16 * 2;

    // Batch scanlines while keeping every physical write 2-pixel aligned. Fewer QSPI
    // transactions matter here because an arbitrary-angle dirty box can be fairly large.
    for (int dy = y1; dy <= y2; ) {
        int rows = y2 - dy + 1;
        if (rows > LVGL_BUF_LINES) rows = LVGL_BUF_LINES;
        rows &= ~1;
        for (int row = 0; row < rows; ++row) {
            const int py = dy + row;
            const int relX2 = 2 * x1 - (SCREEN_W - 1);
            const int relY2 = 2 * py - (SCREEN_H - 1);
            int64_t srcX2Q16 = centerX2Q16 + (int64_t)s_rotCosQ16 * relX2
                                                + (int64_t)s_rotSinQ16 * relY2;
            int64_t srcY2Q16 = centerY2Q16 - (int64_t)s_rotSinQ16 * relX2
                                                + (int64_t)s_rotCosQ16 * relY2;
            lv_color_t *out = s_rotBuf + row * outW;
            for (int dx = 0; dx < outW; ++dx) {
                const int sx = (int)((srcX2Q16 + 65536) >> 17);
                const int sy = (int)((srcY2Q16 + 65536) >> 17);
                out[dx] = (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
                            ? s_frameBuf[sy * SCREEN_W + sx]
                            : black;
                srcX2Q16 += stepX;
                srcY2Q16 += stepY;
            }
        }
        draw_block((int16_t)x1, (int16_t)dy, s_rotBuf, (uint16_t)outW, (uint16_t)rows);
        dy += rows;
    }
}

// LVGL -> panel, applying the chosen rotation while pushing.
//   0°   : straight through.
//   180° : reverse the flat block in place — no scratch buffer.
//   90°/270° : the block transposes (w<->h), so it can't be reversed in place; copy it
//              rotated into a PSRAM scratch buffer. That buffer MUST live in PSRAM — an
//              internal-RAM one starves the mbedTLS handshake and kills the ADS-B feed.
//   other: update the logical framebuffer and inverse-sample the rotated dirty bounds.
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    const int w = (int)(area->x2 - area->x1 + 1);
    const int h = (int)(area->y2 - area->y1 + 1);
    const uint16_t angle = s_rot;
    const bool arbitrary = (angle != 0 && angle != 90 && angle != 180 && angle != 270);
    // Mirror into the logical framebuffer ONLY at non-cardinal angles (it's what
    // flush_arbitrary samples from). At 0/90/180/270 this copy would be pure overhead
    // on every frame for every user; skipping it keeps those paths exactly as before.
    // Switching TO an arbitrary angle invalidates the whole screen (see setRotation),
    // so the framebuffer is fully repopulated before it is ever sampled.
    if (arbitrary && s_frameBuf) {
        for (int row = 0; row < h; ++row) {
            memcpy(s_frameBuf + (area->y1 + row) * SCREEN_W + area->x1,
                   px + row * w, (size_t)w * sizeof(lv_color_t));
        }
    }

    if (arbitrary && s_frameBuf && s_rotBuf) {
        flush_arbitrary(area);
        if (lv_disp_flush_is_last(drv)) frame_done();
        lv_disp_flush_ready(drv);
        return;
    }

    lv_color_t *out = px;
    int16_t  dx = area->x1, dy = area->y1;
    uint16_t dw = (uint16_t)w, dh = (uint16_t)h;

    switch (angle) {
        case 180:
            for (int i = 0, j = w * h - 1; i < j; ++i, --j) { lv_color_t t = px[i]; px[i] = px[j]; px[j] = t; }
            dx = (int16_t)(SCREEN_W - 1 - area->x2);
            dy = (int16_t)(SCREEN_H - 1 - area->y2);
            break;
        case 90:
            if (s_rotBuf) {
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        s_rotBuf[i * h + (h - 1 - j)] = px[j * w + i];
                out = s_rotBuf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = (int16_t)(SCREEN_H - 1 - area->y2); dy = area->x1;
            }
            break;
        case 270:
            if (s_rotBuf) {
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        s_rotBuf[(w - 1 - i) * h + j] = px[j * w + i];
                out = s_rotBuf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = area->y1; dy = (int16_t)(SCREEN_W - 1 - area->x2);
            }
            break;
        default: break;  // 0°
    }
    draw_block(dx, dy, out, dw, dh);
    if (lv_disp_flush_is_last(drv)) frame_done();
    lv_disp_flush_ready(drv);
}

#if !BOARD_PANEL_ST7701
// CO5300 (QSPI) requires 2-pixel-aligned flush windows: even start, odd end.
// Without this, partial-area updates (e.g. the radar sweep) tear / ghost / flicker.
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    (void)drv;
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}
#endif

#if BOARD_HAS_TOUCH
// CST9217 touch -> LVGL pointer. LVGL keeps the last point on release.
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    uint16_t x, y;
    if (touch_read(&x, &y)) {
        int lx = x, ly = y;                              // physical touch -> logical (inverse rotation)
        const uint16_t angle = s_rot;
        switch (angle) {
            case 90:  lx = y;                        ly = SCREEN_H - 1 - x; break;
            case 180: lx = SCREEN_W - 1 - x;         ly = SCREEN_H - 1 - y; break;
            case 270: lx = SCREEN_W - 1 - y;         ly = x; break;
            default:
                if (angle != 0) {
                    const int relX2 = 2 * (int)x - (SCREEN_W - 1);
                    const int relY2 = 2 * (int)y - (SCREEN_H - 1);
                    const int64_t sx2q = (int64_t)(SCREEN_W - 1) * 65536
                                       + (int64_t)s_rotCosQ16 * relX2
                                       + (int64_t)s_rotSinQ16 * relY2;
                    const int64_t sy2q = (int64_t)(SCREEN_H - 1) * 65536
                                       - (int64_t)s_rotSinQ16 * relX2
                                       + (int64_t)s_rotCosQ16 * relY2;
                    lx = (int)((sx2q + 65536) >> 17);
                    ly = (int)((sy2q + 65536) >> 17);
                }
                break;
        }
        if (lx < 0 || lx >= SCREEN_W || ly < 0 || ly >= SCREEN_H) {
            data->state = LV_INDEV_STATE_RELEASED;        // black corners are outside the logical UI
            return;
        }
        data->point.x = (lv_coord_t)lx;
        data->point.y = (lv_coord_t)ly;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif  // BOARD_HAS_TOUCH

namespace display {

bool begin() {
#if BOARD_PANEL_ST7701
    Serial.println("[display] init ST7701 RGB...");
    if (!tca9554_begin()) {                       // LCD reset/CS live on the expander
        Serial.println("[display] TCA9554 expander missing — cannot reach the panel");
        return false;
    }
    s_panel = panel_st7701_init();
    if (!s_panel) {
        Serial.println("[display] panel_st7701_init() FAILED");
        return false;
    }
    void *fb0 = nullptr, *fb1 = nullptr;          // both scanout FBs (see draw_block/compose_step)
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1) == ESP_OK) {
        s_fb[0] = (uint16_t *)fb0;
        s_fb[1] = (uint16_t *)fb1;
    }
    if (!s_fb[1]) Serial.println("[display] WARNING: single framebuffer only — tearing possible");
    // sweep compositor buffers (see compose_step): the clean LVGL image + bearing map
    s_clean = (uint16_t *)heap_caps_calloc((size_t)SCREEN_W * SCREEN_H,
                                           sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    sweep_build_tables();
    if (!s_clean || !s_angMap)
        Serial.println("[display] WARNING: compositor buffers failed; sweep stays in LVGL");
    panel_fill_black();                           // fresh PSRAM framebuffers scan out garbage otherwise
    ledcAttach(PIN_LCD_BL, 20000 /*Hz*/, 10 /*bit*/);   // backlight PWM (demo values)
    setBrightness(BRIGHTNESS_DEFAULT);
#else
    Serial.println("[display] init CO5300 QSPI...");
    s_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK,
                                  PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    s_gfx = new Arduino_CO5300(s_bus, PIN_LCD_RST, 0 /*rotation*/,
                               SCREEN_W, SCREEN_H,
                               LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);
    if (!s_gfx->begin(LCD_QSPI_HZ)) {
        Serial.println("[display] gfx->begin() FAILED");
        return false;
    }
    s_gfx->fillScreen(RGB565_BLACK);
    s_gfx->setBrightness(BRIGHTNESS_DEFAULT);
#endif
    Serial.println("[display] panel up; init LVGL...");

    lv_init();

    // Draw scratch in INTERNAL DMA RAM: rendering anti-aliased graphics into PSRAM is
    // slow (that, not QSPI bandwidth, was the bottleneck). Keep the active buffer in fast
    // internal SRAM; single partial buffer to stay within the internal-RAM budget.
    const size_t buf_px = (size_t)SCREEN_W * LVGL_BUF_LINES;
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_buf2 = nullptr;
    if (!s_buf1) {
        Serial.println("[display] internal draw buffer failed; falling back to PSRAM");
        s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

    // Rotation buffers live in PSRAM so the internal contiguous block needed by TLS remains
    // available. The full logical framebuffer makes inverse sampling at arbitrary angles
    // possible without holes; the smaller scratch holds transposed blocks or output batches.
    s_rotBuf = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    s_frameBuf = (lv_color_t *)heap_caps_calloc((size_t)SCREEN_W * SCREEN_H,
                                                sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!s_rotBuf || !s_frameBuf) {
        Serial.println("[display] WARNING: rotation buffer allocation failed; arbitrary angles unavailable");
    }

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = SCREEN_W;
    s_disp_drv.ver_res  = SCREEN_H;
    s_disp_drv.flush_cb = flush_cb;
#if !BOARD_PANEL_ST7701
    s_disp_drv.rounder_cb = rounder_cb;     // CO5300 needs 2-px-aligned windows
#endif
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

#if BOARD_HAS_TOUCH
    // Touch input -> LVGL pointer indev (drives tap-to-inspect + swipe).
    if (touch_begin()) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type = LV_INDEV_TYPE_POINTER;
        s_indev_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_indev_drv);
        Serial.println("[display] touch registered (" BOARD_NAME ")");
    }
#else
    Serial.println("[display] no touch on " BOARD_NAME "; the BOOT button navigates");
#endif

    Serial.printf("[display] PSRAM free: %u KB\n", (unsigned)(ESP.getFreePsram() / 1024));
    ui_create();                   // M3: radar/list/stats views + tap-to-inspect
    Serial.println("[display] LVGL ready");
    return true;
}

void loop() {
    lv_timer_handler();
#if BOARD_PANEL_ST7701
    compose_step();                   // sweep + fresh LVGL content -> scanout, vsync-swapped
#endif
}

#if BOARD_PANEL_ST7701
// 0..255 mapped onto the 10-bit backlight PWM (the panel itself has no brightness cmd)
void setBrightness(uint8_t v) { ledcWrite(PIN_LCD_BL, ((uint32_t)v * 1023 + 127) / 255); }

void sweepConfig(uint16_t ring565, uint16_t lead565, bool enabled) {
    s_swRing565 = ring565;
    s_swLead565 = lead565;
    s_swEnabled = enabled && s_clean && s_angMap && s_fb[1];
}
void sweepSetVisible(bool onRadarView) { s_swVisible = onRadarView; }
bool sweepCompositor() { return s_clean && s_angMap && s_fb[1]; }
#else
void setBrightness(uint8_t v) { if (s_gfx) s_gfx->setBrightness(v); }

void sweepConfig(uint16_t, uint16_t, bool) {}
void sweepSetVisible(bool) {}
bool sweepCompositor() { return false; }
#endif

void setRotation(uint16_t degrees) {
    uint16_t normalized = (uint16_t)(degrees % 360);
    if ((normalized == 90 || normalized == 270) && !s_rotBuf) {
        Serial.println("[display] quarter-turn rotation unavailable without the PSRAM scratch buffer");
        normalized = 0;
    }
    if (normalized != 0 && normalized != 90 && normalized != 180 && normalized != 270
        && (!s_frameBuf || !s_rotBuf)) {
        Serial.println("[display] arbitrary rotation unavailable without both PSRAM buffers");
        normalized = 0;
    }
    if (normalized == s_rot) return;
    const float radians = normalized * ((float)M_PI / 180.0f);
    s_rotCos = cosf(radians);
    s_rotSin = sinf(radians);
    s_rotCosQ16 = (int32_t)lroundf(s_rotCos * 65536.0f);
    s_rotSinQ16 = (int32_t)lroundf(s_rotSin * 65536.0f);
    s_rot = normalized;
    panel_fill_black();                // clear pixels no longer covered after an angle change
    lv_obj_t *scr = lv_scr_act();
    if (scr) lv_obj_invalidate(scr);   // full repaint in the new orientation
}
uint16_t rotation() { return s_rot; }

uint32_t inactiveMs() { return lv_disp_get_inactive_time(NULL); }

} // namespace display
