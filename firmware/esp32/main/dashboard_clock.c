#include "dashboard_clock.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* POSIX TZ sign is inverted: "UTC-2" = UTC+2 hours ahead of UTC.
 * Applied once at first call; can be overridden by a proper NTP/portal config
 * later if timezone support is added. */
#define DASHBOARD_CLOCK_DEFAULT_TZ "UTC-2"

static void ensure_timezone_set(void) {
  static int s_done = 0;
  if (s_done) return;
  s_done = 1;
  if (getenv("TZ") == NULL) {
    setenv("TZ", DASHBOARD_CLOCK_DEFAULT_TZ, 1);
    tzset();
  }
}

void dashboard_clock_read(lm_ctrl_clock_t *clock) {
  time_t now;
  struct tm tm_info;

  ensure_timezone_set();

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
