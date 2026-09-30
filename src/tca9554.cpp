// Minimal TCA9554PWR IO-expander driver. ESP32-S3-LCD-2.8C only (BOARD_PANEL_ST7701).
#include "tca9554.h"
#include "config.h"

#if BOARD_PANEL_ST7701
#include <Arduino.h>
#include <Wire.h>

// Register map (TCA9554 datasheet): 0x01 = output levels, 0x03 = pin direction.
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;

static bool    s_ok = false;
static uint8_t s_shadow = 0;   // last written output byte (avoids a read-modify-write on the bus)

static bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission((uint8_t)I2C_ADDR_EXPANDER);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool tca9554_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);   // idempotent (IMU/RTC also begin it)
    // Set the output levels BEFORE switching the pins to output mode — the chip powers
    // up with the output register all-high, which would chirp the buzzer (EXIO8).
    s_shadow = (uint8_t)((1u << (EXIO_LCD_RST - 1)) | (1u << (EXIO_LCD_CS - 1)));  // RST high (run), CS high (deselected)
    s_ok = writeReg(REG_OUTPUT, s_shadow) && writeReg(REG_CONFIG, 0x00);           // all pins output
    Serial.printf("[exio] TCA9554 %s\n", s_ok ? "ready" : "NOT FOUND");
    return s_ok;
}

bool tca9554_ok() { return s_ok; }

void tca9554_set(uint8_t exio, bool hi) {
    if (!s_ok || exio < 1 || exio > 8) return;
    const uint8_t bit = (uint8_t)(1u << (exio - 1));
    const uint8_t next = hi ? (uint8_t)(s_shadow | bit) : (uint8_t)(s_shadow & ~bit);
    if (writeReg(REG_OUTPUT, next)) s_shadow = next;
}

#endif  // BOARD_PANEL_ST7701
