#include "brew_counter.h"

#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "nvs.h"

#include "lm_ctrl_nvs_keys.h"

#define BREW_MIN_DURATION_US (10LL * 1000LL * 1000LL) /* 10 seconds */

static int current_yday(void) {
  time_t now;
  struct tm tm_info;

  time(&now);
  if (now < 1672531200LL) {
    return -1; /* clock not synced */
  }
  localtime_r(&now, &tm_info);
  return tm_info.tm_yday;
}

void brew_counter_init(lm_ctrl_brew_counter_t *counter) {
  if (counter == NULL) {
    return;
  }
  memset(counter, 0, sizeof(*counter));
  counter->last_date_yday = -1;
}

void brew_counter_load(lm_ctrl_brew_counter_t *counter) {
  nvs_handle_t handle;
  uint16_t count = 0;
  int32_t  yday  = -1;
  size_t   sz;

  if (counter == NULL) {
    return;
  }

  if (nvs_open(LM_CTRL_BREW_COUNTER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }

  sz = sizeof(count);
  if (nvs_get_blob(handle, LM_CTRL_BREW_COUNTER_KEY_COUNT, &count, &sz) == ESP_OK &&
      sz == sizeof(count)) {
    counter->count = count;
  }
  sz = sizeof(yday);
  if (nvs_get_blob(handle, LM_CTRL_BREW_COUNTER_KEY_DATE, &yday, &sz) == ESP_OK &&
      sz == sizeof(yday)) {
    counter->last_date_yday = (int)yday;
  }
  nvs_close(handle);

  /* Reset if a new day has started since last save */
  const int today = current_yday();
  if (today >= 0 && today != counter->last_date_yday) {
    counter->count          = 0;
    counter->last_date_yday = today;
    brew_counter_save(counter);
  }
}

void brew_counter_update(lm_ctrl_brew_counter_t *counter, bool brewing_active) {
  if (counter == NULL) {
    return;
  }

  /* Midnight reset */
  const int today = current_yday();
  if (today >= 0 && counter->last_date_yday >= 0 && today != counter->last_date_yday) {
    counter->count           = 0;
    counter->last_date_yday  = today;
    counter->brew_counted    = false;
    brew_counter_save(counter);
  }

  const int64_t now_us = esp_timer_get_time();

  if (brewing_active && !counter->currently_brewing) {
    /* Rising edge — record start */
    counter->currently_brewing = true;
    counter->brew_start_us     = now_us;
    counter->brew_counted      = false;
  } else if (!brewing_active && counter->currently_brewing) {
    /* Falling edge — count if long enough */
    counter->currently_brewing = false;
    if (!counter->brew_counted) {
      const int64_t elapsed = now_us - counter->brew_start_us;
      if (elapsed >= BREW_MIN_DURATION_US) {
        counter->count++;
        counter->brew_counted    = true;
        counter->last_date_yday  = today;
        brew_counter_save(counter);
      }
    }
  }
}

uint16_t brew_counter_get(const lm_ctrl_brew_counter_t *counter) {
  return counter != NULL ? counter->count : 0U;
}

void brew_counter_save(const lm_ctrl_brew_counter_t *counter) {
  nvs_handle_t handle;
  int32_t yday;

  if (counter == NULL) {
    return;
  }
  if (nvs_open(LM_CTRL_BREW_COUNTER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }

  nvs_set_blob(handle, LM_CTRL_BREW_COUNTER_KEY_COUNT, &counter->count, sizeof(counter->count));
  yday = (int32_t)counter->last_date_yday;
  nvs_set_blob(handle, LM_CTRL_BREW_COUNTER_KEY_DATE, &yday, sizeof(yday));
  nvs_commit(handle);
  nvs_close(handle);
}
