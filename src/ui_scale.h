#pragma once
// UI geometry/font scaling for panels bigger than the 466 px AMOLED the UI was laid out on.
//
// Every hand-placed position and size in the UI goes through UI_S(); fonts go through
// UI_F<size>. On every board with UI_SCALE_PERMILLE 1000 (all the S3 boards) both are the
// identity, so those layouts are byte-for-byte what they were. The 800 px P4 panel scales
// geometry by 800/466 and steps each font up a tier — LVGL font sizes are discrete, so
// they cannot follow the scale factor exactly.
//
// Not everything scales: stroke widths stay put, and values tied to an IMAGE's own pixel
// size (the weather tile, aircraft photos) must not be scaled — only the chrome around
// them. See the weather section of ui.cpp.
#include "config.h"
#include <lvgl.h>

#define UI_S(v) ((lv_coord_t)(((int32_t)(v) * UI_SCALE_PERMILLE + ((v) < 0 ? -500 : 500)) / 1000))

#if UI_BIG_PANEL
#  define UI_F12 (&lv_font_montserrat_20)
#  define UI_F14 (&lv_font_montserrat_24)
#  define UI_F16 (&lv_font_montserrat_28)
#  define UI_F18 (&lv_font_montserrat_32)
#  define UI_F20 (&lv_font_montserrat_32)
#  define UI_F28 (&lv_font_montserrat_48)
#else
#  define UI_F12 (&lv_font_montserrat_12)
#  define UI_F14 (&lv_font_montserrat_14)
#  define UI_F16 (&lv_font_montserrat_16)
#  define UI_F18 (&lv_font_montserrat_18)
#  define UI_F20 (&lv_font_montserrat_20)
#  define UI_F28 (&lv_font_montserrat_28)
#endif
