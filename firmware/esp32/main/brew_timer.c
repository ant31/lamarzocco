#include "brew_timer.h"

#include <string.h>
#include "esp_timer.h"

void brew_timer_init(lm_ctrl_brew_timer_t *timer) {
  if (timer == NULL) return;
  memset(timer, 0, sizeof(*timer));
}

void brew_timer_start(lm_ctrl_brew_timer_t *timer) {
  if (timer == NULL) return;
  if (timer->running) return;
  timer->start_us = esp_timer_get_time() - timer->elapsed_us;
  timer->running  = true;
}

void brew_timer_stop(lm_ctrl_brew_timer_t *timer) {
  if (timer == NULL) return;
  if (!timer->running) return;
  timer->elapsed_us = esp_timer_get_time() - timer->start_us;
  timer->running    = false;
}

void brew_timer_reset(lm_ctrl_brew_timer_t *timer) {
  if (timer == NULL) return;
  memset(timer, 0, sizeof(*timer));
}

void brew_timer_tick(lm_ctrl_brew_timer_t *timer, bool brewing_active) {
  if (timer == NULL) return;

  /* Auto-start on brewing rising edge unless the user has taken manual control */
  if (brewing_active && !timer->running && !timer->manual_override) {
    brew_timer_start(timer);
  }

  if (timer->running) {
    timer->elapsed_us = esp_timer_get_time() - timer->start_us;
  }
}

float brew_timer_elapsed_s(const lm_ctrl_brew_timer_t *timer) {
  if (timer == NULL) return 0.0f;
  return (float)timer->elapsed_us / 1000000.0f;
}
