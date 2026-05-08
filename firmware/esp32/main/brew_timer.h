#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool    running;
  bool    manual_override; /**< user pressed Start/Stop; disables auto-start */
  int64_t start_us;        /**< esp_timer_get_time() snapshot when started */
  int64_t elapsed_us;      /**< updated every tick while running */
} lm_ctrl_brew_timer_t;

/** Zero-initialise a brew timer struct. */
void brew_timer_init(lm_ctrl_brew_timer_t *timer);
/** Start timing from now. */
void brew_timer_start(lm_ctrl_brew_timer_t *timer);
/** Freeze elapsed_us and stop. */
void brew_timer_stop(lm_ctrl_brew_timer_t *timer);
/** Full reset to zero. */
void brew_timer_reset(lm_ctrl_brew_timer_t *timer);
/**
 * Call every ~100 ms.
 * When running, updates elapsed_us.
 * When brewing_active transitions high and manual_override is false,
 * the timer is auto-started.
 */
void brew_timer_tick(lm_ctrl_brew_timer_t *timer, bool brewing_active);
/** Elapsed seconds as a float (e.g. 12.3). */
float brew_timer_elapsed_s(const lm_ctrl_brew_timer_t *timer);

#ifdef __cplusplus
}
#endif
