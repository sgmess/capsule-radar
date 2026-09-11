// Board bring-up probe for round CO5300/SH8601-class QSPI AMOLED boards.
//
// Answers, in order, the three questions that a new board port actually turns on:
//   1. Are the I2C pins right, and what is actually on the bus?
//   2. Are the QSPI pins right -- does the panel light up and fill edge to edge?
//   3. What are the panel's column/row gaps, and is touch mirrored?
//
// Edit the CONFIGURE block, flash, watch the serial log, then use the on-screen
// nudge tool. Whatever it settles on is your LCD_COL_OFFSET / LCD_ROW_OFFSET.
//
// Three traps this tool exists to avoid, all of which cost real time to find:
//
//   * Do NOT call gfx->begin() twice. Arduino_GFX has no teardown, and the second
//     call aborts with "spi_bus_initialize(800): SPI bus already initialized",
//     which shows up as a boot loop with a half-drawn screen -- easily mistaken
//     for a hardware fault. That is why the offset sweep here shifts the image in
//     software instead of re-constructing the panel object.
//
//   * These panels need 2-pixel-aligned address windows (even start, odd end).
//     Unaligned 1px draws are silently dropped, so a test card made of 1px circles
//     and crosshairs renders as almost nothing and looks like a dead panel. Phase 2
//     composes the entire frame in PSRAM and blits it in ONE aligned full-screen
//     window, so single-pixel detail is exact and a 1px nudge really is 1px.
//
//   * Some touch controllers share the panel reset line and are absent from I2C
//     until the display has been initialised. Phase 1 therefore scans the bus twice,
//     before and after panel init, and reports the difference.
//
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <math.h>

// ---------------------------------------------------------------- CONFIGURE --
// Take these from the vendor demo or the Arduino core board variant in
// ~/.platformio/packages/framework-arduinoespressif32/variants/<board>/pins_arduino.h
// Never guess them; this tool is how you confirm them.
#define LCD_CS      9
#define LCD_SCK     10
#define LCD_D0      11
#define LCD_D1      12
#define LCD_D2      13
#define LCD_D3      14
#define LCD_RST     21
#define PANEL_W     466
#define PANEL_H     466
#define QSPI_HZ     40000000     // start at the vendor rate; raise only once it works

#define I2C_SDA     47
#define I2C_SCL     48
#define TOUCH_ADDR  0x38         // FocalTech FT3168/FT5x06 family; CST9217 is 0x5A
// -----------------------------------------------------------------------------

static Arduino_CO5300 *gfx = nullptr;
static uint16_t *fb = nullptr;
static int dx = 0, dy = 0;

// ---- phase 1: what is on the I2C bus ----------------------------------------
static uint32_t scan(const char *when) {
  uint32_t mask_lo = 0;
  Serial.printf("[i2c] scan %s: ", when);
  int n = 0;
  for (uint8_t a = 1; a < 0x7F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", a);
      if (a < 64) mask_lo |= (1UL << a);
      n++;
    }
  }
  Serial.printf(" (%d found)\n", n);
  return mask_lo;
}

static void identify(uint8_t addr) {
  struct { uint8_t addr; const char *what; } known[] = {
    {0x38, "FT3168/FT5x06 touch"}, {0x5A, "CST9217 touch"}, {0x15, "CST816/CST820 touch"},
    {0x51, "PCF85063 RTC"},        {0x6B, "QMI8658 IMU"},   {0x6A, "QMI8658 IMU (alt)"},
    {0x34, "AXP2101 PMIC"},        {0x18, "ES8311 codec"},  {0x58, "LC76G GNSS"},
  };
  for (auto &k : known) if (k.addr == addr) { Serial.printf("        0x%02X = %s\n", addr, k.what); return; }
  Serial.printf("        0x%02X = unknown\n", addr);
}

// ---- phase 2/3: aligned test card in PSRAM ----------------------------------
static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H) fb[y * PANEL_W + x] = c;
}
static void box(int x0, int x1, int y0, int y1, uint16_t c) {
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) px(x, y, c);
}
static void ring(int cx, int cy, int r, int thick, uint16_t c) {
  for (int y = -r - 1; y <= r + 1; y++)
    for (int x = -r - 1; x <= r + 1; x++) {
      float d = sqrtf((float)(x * x + y * y));
      if (d <= r && d >= r - thick) px(cx + x, cy + y, c);
    }
}

static void render() {
  memset(fb, 0, (size_t)PANEL_W * PANEL_H * 2);
  const uint16_t WHITE = 0xFFFF, GREEN = 0x07E0, RED = 0xF800, CYAN = 0x07FF;
  const int r = PANEL_W / 2 - 1;
  const int cx = PANEL_W / 2 + dx, cy = PANEL_H / 2 + dy;

  ring(cx, cy, r, 3, WHITE);                    // should sit exactly on the rim
  ring(cx, cy, r - 82, 2, GREEN);
  box(cx - 200, cx + 200, cy, cy + 1, GREEN);   // crosshair
  box(cx, cx + 1, cy - 200, cy + 200, GREEN);

  // Ticks on the outermost logical row/column, where a round panel is widest.
  // A missing tick means the image is pushed off the panel on that side.
  box(cx - r, cx - r + 2, cy - 30, cy + 30, RED);    // far left  (x = 0)
  box(cx + r - 2, cx + r, cy - 30, cy + 30, RED);    // far right (x = W-1)
  box(cx - 30, cx + 30, cy - r, cy - r + 2, CYAN);   // top       (y = 0)
  box(cx - 30, cx + 30, cy + r - 2, cy + r, CYAN);   // bottom    (y = H-1)

  gfx->draw16bitRGBBitmap(0, 0, fb, PANEL_W, PANEL_H);   // one aligned full-screen window
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(3);
  gfx->setCursor(PANEL_W / 2 - 85, PANEL_H / 2 + 70);
  gfx->printf("x%+d y%+d ", dx, dy);
  Serial.printf("[gap] LCD_COL_OFFSET=%d  LCD_ROW_OFFSET=%d\n", dx, dy);
}

void setup() {
  Serial.begin(115200);
  delay(2500);
  Serial.println("\n\n=== board bring-up probe ===");
  Serial.printf("PSRAM free: %u bytes\n", (unsigned)ESP.getFreePsram());

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  const uint32_t before = scan("BEFORE panel init");

  Serial.printf("[lcd] QSPI CS=%d SCK=%d D0..D3=%d,%d,%d,%d RST=%d\n",
                LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_RST);
  Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
  // Gaps are 0 here on purpose: phase 3 finds them by shifting in software.
  gfx = new Arduino_CO5300(bus, LCD_RST, 0, PANEL_W, PANEL_H, 0, 0, 0, 0);
  Serial.printf("[lcd] begin(): %s\n", gfx->begin(QSPI_HZ) ? "ok" : "FAILED");
  gfx->setBrightness(200);
  // NOTE: begin() is never called again. See the header comment.

  const uint32_t after = scan("AFTER panel init");
  for (uint8_t a = 1; a < 64; a++) if (after & (1UL << a)) identify(a);
  const uint32_t appeared = after & ~before;
  if (appeared) {
    Serial.println("[i2c] NOTE: device(s) appeared only after the panel reset -- their reset");
    Serial.println("      line is tied to the LCD reset, so the display must be initialised");
    Serial.println("      before touch_begin() in the firmware.");
  }

  fb = (uint16_t *)heap_caps_malloc((size_t)PANEL_W * PANEL_H * 2, MALLOC_CAP_SPIRAM);
  if (!fb) { Serial.println("[!] PSRAM alloc failed"); return; }

  Serial.println("\n[gap] tap left/right of centre to nudge X, above/below to nudge Y.");
  Serial.println("      Settle where the white ring is even all round and all four");
  Serial.println("      edge ticks are visible -- those are your panel gaps.");
  Serial.println("[touch] raw coords are logged; if the drawn box does not land under");
  Serial.println("        your finger, you need the mirror/swap flags.");
  render();
}

static uint32_t lastTap = 0;
void loop() {
  delay(30);
  if (!fb || millis() - lastTap < 260) return;

  uint8_t b[7];
  Wire.beginTransmission(TOUCH_ADDR); Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom((int)TOUCH_ADDR, 7) < 7) return;
  for (int i = 0; i < 7; i++) b[i] = Wire.read();
  if (!(b[2] & 0x0F)) return;          // FocalTech: touch count
  if ((b[3] >> 6) == 1) return;        // lift-up event

  const int x = ((b[3] & 0x0F) << 8) | b[4];
  const int y = ((b[5] & 0x0F) << 8) | b[6];
  Serial.printf("[touch] raw x=%3d y=%3d\n", x, y);
  lastTap = millis();

  const int ox = x - PANEL_W / 2, oy = y - PANEL_H / 2;
  if (abs(ox) > abs(oy)) dx += (ox > 0) ? 1 : -1;
  else                   dy += (oy > 0) ? 1 : -1;
  dx = constrain(dx, -20, 20);
  dy = constrain(dy, -20, 20);
  render();
}
