// Alert "pings" for the ESP32-S3-LCD-2.8C: no ES8311/speaker on this board, just an
// on/off active buzzer behind the TCA9554 expander (EXIO8). Implements the audio.h API
// so main.cpp is unchanged. Every buzzer toggle is an I2C write, so unlike the ES8311
// playback task everything here stays on core 1: audio_play() is called from loop(),
// and the beep pattern is stepped by an LVGL timer (also core 1).
#include "audio.h"
#include "config.h"

#if BOARD_HAS_BUZZER
#include "tca9554.h"
#include <lvgl.h>

static bool s_ok = false;
static bool s_muted = false;
static lv_timer_t *s_timer = nullptr;

// Beep patterns: segment durations in ms, alternating on/off starting with ON; 0 ends.
static const uint16_t PAT_NEW[]   = { 45, 0 };                 // soft single beep
static const uint16_t PAT_ALERT[] = { 70, 80, 70, 0 };         // urgent double beep
static const uint16_t PAT_TEST[]  = { 2000, 0 };               // by-ear self-test
static const uint16_t *s_pat = nullptr;
static int s_idx = 0;

static void buzz(bool on) { tca9554_set(EXIO_BUZZER, on); }

static void pattern_step(lv_timer_t *) {
    s_idx++;
    if (!s_pat || s_pat[s_idx] == 0) {
        buzz(false);
        lv_timer_pause(s_timer);
        s_pat = nullptr;
        return;
    }
    buzz((s_idx & 1) == 0);
    lv_timer_set_period(s_timer, s_pat[s_idx]);
}

static void pattern_start(const uint16_t *pat) {
    if (!s_timer) return;
    s_pat = pat;
    s_idx = 0;
    buzz(true);
    lv_timer_set_period(s_timer, pat[0]);
    lv_timer_reset(s_timer);
    lv_timer_resume(s_timer);
}

bool audio_begin() {
    // display::begin() has already run (LVGL up) and initialized the expander
    s_ok = tca9554_ok();
    if (s_ok) {
        s_timer = lv_timer_create(pattern_step, 1000, nullptr);
        lv_timer_pause(s_timer);
    }
    return s_ok;
}

bool audio_present() { return s_ok; }
void audio_set_volume(int) {}                    // on/off buzzer: no volume control
void audio_set_muted(bool muted) { s_muted = muted; }

void audio_play(AudioCue cue) {
    if (!s_ok || s_muted) return;
    if (s_pat) return;                           // a beep is already sounding; skip
    pattern_start(cue == AUDIO_ALERT ? PAT_ALERT : PAT_NEW);
}

void audio_selftest() {                          // ignores mute, like the ES8311 version
    if (!s_ok) return;
    pattern_start(PAT_TEST);
}

#endif  // BOARD_HAS_BUZZER
