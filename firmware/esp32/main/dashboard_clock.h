#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current wall-clock time read from the SNTP-synced system clock. */
typedef struct {
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
  bool    valid; /**< false when the system clock is not yet set (pre-SNTP). */
} lm_ctrl_clock_t;

/** Read the current local time into clock. Sets valid=false if SNTP has not synced. */
void dashboard_clock_read(lm_ctrl_clock_t *clock);

#ifdef __cplusplus
}
#endif
