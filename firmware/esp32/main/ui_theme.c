#include "ui_theme.h"

#include <stddef.h>

/* Built-in theme table — background, foreground, accent, muted, button, ring */
static const lm_ctrl_ui_theme_t k_themes[LM_CTRL_UI_THEME_COUNT] = {
  {
    "Espresso",
    LV_COLOR_MAKE(0x11, 0x0B, 0x08), /* bg  — near-black warm brown (original) */
    LV_COLOR_MAKE(0xF4, 0xEF, 0xE7), /* fg  — warm white */
    LV_COLOR_MAKE(0xFF, 0xC6, 0x85), /* accent — amber */
    LV_COLOR_MAKE(0xB7, 0xA0, 0x8B), /* muted */
    LV_COLOR_MAKE(0x3A, 0x2A, 0x21), /* button */
    LV_COLOR_MAKE(0x92, 0x63, 0x33), /* ring */
  },
  {
    "Dark",
    LV_COLOR_MAKE(0x11, 0x11, 0x11),
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    LV_COLOR_MAKE(0xC8, 0x96, 0x3E),
    LV_COLOR_MAKE(0x88, 0x88, 0x88),
    LV_COLOR_MAKE(0x2A, 0x2A, 0x2A),
    LV_COLOR_MAKE(0x55, 0x55, 0x55),
  },
  {
    "Light",
    LV_COLOR_MAKE(0xF5, 0xF5, 0xF5),
    LV_COLOR_MAKE(0x11, 0x11, 0x11),
    LV_COLOR_MAKE(0xC8, 0x96, 0x3E),
    LV_COLOR_MAKE(0x66, 0x66, 0x66),
    LV_COLOR_MAKE(0xDD, 0xDD, 0xDD),
    LV_COLOR_MAKE(0xAA, 0xAA, 0xAA),
  },
  {
    "Blue",
    LV_COLOR_MAKE(0x0D, 0x1B, 0x2A),
    LV_COLOR_MAKE(0xE0, 0xE8, 0xF0),
    LV_COLOR_MAKE(0x4A, 0x90, 0xD9),
    LV_COLOR_MAKE(0x60, 0x80, 0xA0),
    LV_COLOR_MAKE(0x1A, 0x30, 0x48),
    LV_COLOR_MAKE(0x2A, 0x50, 0x70),
  },
  {
    "Green",
    LV_COLOR_MAKE(0x0D, 0x2A, 0x14),
    LV_COLOR_MAKE(0xE0, 0xF0, 0xE4),
    LV_COLOR_MAKE(0x4A, 0xD9, 0x7A),
    LV_COLOR_MAKE(0x60, 0xA0, 0x70),
    LV_COLOR_MAKE(0x1A, 0x48, 0x22),
    LV_COLOR_MAKE(0x2A, 0x70, 0x38),
  },
};

const lm_ctrl_ui_theme_t *ui_theme_get(uint8_t index) {
  if (index >= LM_CTRL_UI_THEME_COUNT) {
    index = 0;
  }
  return &k_themes[index];
}

void ui_theme_apply(uint8_t index, lv_obj_t *screen) {
  const lm_ctrl_ui_theme_t *theme = ui_theme_get(index);

  if (screen == NULL) {
    return;
  }

  lv_obj_set_style_bg_color(screen, theme->background, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_report_style_change(NULL);
}
