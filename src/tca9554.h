#pragma once
// Minimal TCA9554PWR IO-expander driver (I2C). ESP32-S3-LCD-2.8C only: the board
// routes the LCD reset/CS and the buzzer through this chip. Same bus rule as every
// other I2C device: core 1 only.
#include <stdint.h>

bool tca9554_begin();                    // set safe outputs (LCD idle, buzzer off); false if absent
bool tca9554_ok();                       // begin() succeeded
void tca9554_set(uint8_t exio, bool hi); // drive EXIO pin 1..8
