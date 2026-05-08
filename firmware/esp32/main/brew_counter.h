#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t count;
  int      last_date_yday; /* tm_yday at last reset; -1 = unknown */
  int64_t  brew_start_us;  /* esp_timer timestamp of brew rising edge */
  bool     currently_brewing;
  bool     brew_counted;   /* guard: one count per continuous brew session */
} lm_ctrl_brew_counter_t;

/** Initialise a counter struct with default values. */
void brew_counter_init(lm_ctrl_brew_counter_t *counter);
/** Load persisted count and date from NVS; resets if a new day has started. */
void brew_counter_load(lm_ctrl_brew_counter_t *counter);
/** Call every ~100 ms with the current brewing state.
 *  Increments count when a brew session lasts ≥ 10 s and saves to NVS. */
void brew_counter_update(lm_ctrl_brew_counter_t *counter, bool brewing_active);
/** Return the current daily brew count. */
uint16_t brew_counter_get(const lm_ctrl_brew_counter_t *counter);
/** Persist the current count and date to NVS. */
void brew_counter_save(const lm_ctrl_brew_counter_t *counter);

#ifdef __cplusplus
}
#endif
