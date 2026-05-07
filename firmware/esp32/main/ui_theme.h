#pragma once

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of built-in color themes. */
#define LM_CTRL_UI_THEME_COUNT 5

/** A single named color palette for the controller UI. */
typedef struct {
  const char *name;
  lv_color_t  background;
  lv_color_t  foreground;
  lv_color_t  accent;
  lv_color_t  muted;
  lv_color_t  button;
  lv_color_t  ring;
} lm_ctrl_ui_theme_t;

/** Return a pointer to the built-in theme at the given index (clamped). */
const lm_ctrl_ui_theme_t *ui_theme_get(uint8_t index);

/** Apply the theme at the given index to the current LVGL screen.
 *  Invalidates all styles so changes are reflected on the next redraw. */
void ui_theme_apply(uint8_t index, lv_obj_t *screen);

#ifdef __cplusplus
}
#endif
