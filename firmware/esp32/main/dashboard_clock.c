#include "dashboard_clock.h"

#include <string.h>
#include <time.h>

void dashboard_clock_read(lm_ctrl_clock_t *clock) {
  time_t now;
  struct tm tm_info;

  if (clock == NULL) {
    return;
  }

  memset(clock, 0, sizeof(*clock));
  time(&now);

  /* Treat anything before 2023-01-01 as unsynced */
  if (now < 1672531200LL) {
    clock->valid = false;
    return;
  }

  localtime_r(&now, &tm_info);
  clock->hours   = (uint8_t)tm_info.tm_hour;
  clock->minutes = (uint8_t)tm_info.tm_min;
  clock->seconds = (uint8_t)tm_info.tm_sec;
  clock->valid   = true;
}
