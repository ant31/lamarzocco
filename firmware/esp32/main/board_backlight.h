#pragma once

#include "esp_err.h"

/** Initialize the PWM backlight driver for the round display. */
esp_err_t lm_ctrl_backlight_init(void);
/** Set the display backlight brightness in percent from 0 to 100. */
esp_err_t lm_ctrl_backlight_set(int brightness_percent);
/** Turn the display backlight on using the default brightness. */
esp_err_t lm_ctrl_backlight_on(void);
/** Turn the display backlight fully off. */
esp_err_t lm_ctrl_backlight_off(void);
/** Set the display backlight to one of five discrete levels (0 = off … 4 = full). */
esp_err_t lm_ctrl_backlight_set_level(uint8_t level_0_to_4);
