/**
 * Runtime coordinator for controller state, sync scheduling, and UI view data.
 *
 * This module keeps the event loop logic out of app_main so bootstrapping stays
 * small and the runtime behaviour is easier to reason about and test.
 */
#include "controller_runtime.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "board_haptic.h"
#include "board_leds.h"
#include "board_power.h"
#include "controller_connectivity.h"
#include "machine_link.h"
#include "machine_link_policy.h"
#include "wifi_setup.h"

static const char *TAG = "lm_ctrl_runtime";
static const int64_t CLOUD_PROBE_INTERVAL_US = 60LL * 1000LL * 1000LL;
static const int64_t BLE_VALUE_REFRESH_INTERVAL_US = 15LL * 1000LL * 1000LL;
static const int64_t CLOUD_VALUE_REFRESH_INTERVAL_US = 60LL * 1000LL * 1000LL;
static const int64_t LOCAL_VALUE_HOLD_US = 30LL * 1000LL * 1000LL;
/** How long to wait for a confirm gesture before reverting a pending edit. */
static const int64_t PENDING_EDIT_TIMEOUT_US = 5LL * 1000LL * 1000LL;
static const int64_t HEAT_REFRESH_INTERVAL_US = 5LL * 1000LL * 1000LL;
static const int64_t HEAT_REFRESH_INITIAL_DELAY_US = 2LL * 1000LL * 1000LL;
static const int64_t HEAT_REFRESH_TIMEOUT_US = 120LL * 1000LL * 1000LL;

static void reset_heat_state(lm_ctrl_runtime_heat_state_t *heat_state) {
  if (heat_state == NULL) {
    return;
  }

  lm_ctrl_heat_session_reset(&heat_state->session);
  heat_state->last_rendered_remaining_s = -1;
  heat_state->last_rendered_progress_permille = -1;
}

static void reset_shot_timer_state(lm_ctrl_runtime_shot_timer_state_t *shot_timer_state) {
  lm_ctrl_shot_timer_reset(shot_timer_state);
}

static void clear_heat_refresh(lm_ctrl_runtime_heat_refresh_t *heat_refresh) {
  if (heat_refresh == NULL) {
    return;
  }

  heat_refresh->until_us = 0;
  heat_refresh->next_request_us = 0;
}

static void arm_heat_refresh(
  lm_ctrl_runtime_heat_refresh_t *heat_refresh,
  int64_t initial_delay_us
) {
  const int64_t now_us = esp_timer_get_time();

  if (heat_refresh == NULL) {
    return;
  }

  heat_refresh->until_us = now_us + HEAT_REFRESH_TIMEOUT_US;
  heat_refresh->next_request_us = now_us + initial_delay_us;
}

static bool should_show_heat_display(void) {
  lm_ctrl_wifi_info_t wifi_info = {0};

  lm_ctrl_wifi_get_info(&wifi_info);
  return wifi_info.heat_display_enabled;
}

static int64_t current_epoch_ms(void) {
  struct timeval tv = {0};

  if (gettimeofday(&tv, NULL) != 0) {
    return 0;
  }
  if (tv.tv_sec < 1700000000) {
    return 0;
  }

  return ((int64_t)tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
}

static int64_t heat_state_remaining_us(const lm_ctrl_runtime_heat_state_t *heat_state) {
  return heat_state != NULL
    ? lm_ctrl_heat_session_remaining_us(&heat_state->session, esp_timer_get_time())
    : 0;
}

static int32_t heat_state_remaining_seconds(const lm_ctrl_runtime_heat_state_t *heat_state) {
  int64_t remaining_us = heat_state_remaining_us(heat_state);
  return remaining_us > 0 ? (int32_t)((remaining_us + 999999LL) / 1000000LL) : 0;
}

static uint16_t heat_state_progress_permille(const lm_ctrl_runtime_heat_state_t *heat_state) {
  return heat_state != NULL
    ? lm_ctrl_heat_session_progress_permille(&heat_state->session, esp_timer_get_time())
    : 0;
}

static void maybe_refresh_shot_timer(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  lm_ctrl_shot_timer_info_t shot_timer = {0};

  if (runtime == NULL) {
    return;
  }

  if (lm_ctrl_wifi_get_shot_timer_info(&shot_timer) &&
      lm_ctrl_shot_timer_update(&runtime->shot_timer_state, &shot_timer)) {
    if (needs_render != NULL) {
      *needs_render = true;
    }
  }
}

static bool sync_heat_state_deadline(
  lm_ctrl_runtime_heat_state_t *heat_state,
  int64_t observed_epoch_ms,
  int64_t ready_epoch_ms
) {
  if (heat_state == NULL || ready_epoch_ms <= 0) {
    return false;
  }

  return lm_ctrl_heat_session_apply(
    &heat_state->session,
    true,
    true,
    esp_timer_get_time(),
    current_epoch_ms(),
    observed_epoch_ms,
    ready_epoch_ms
  );
}

static void sync_heat_state(lm_ctrl_runtime_t *runtime) {
  lm_ctrl_machine_heat_info_t heat_info = {0};
  bool have_machine_eta = false;

  if (runtime == NULL) {
    return;
  }
  if (!should_show_heat_display()) {
    reset_heat_state(&runtime->heat_state);
    clear_heat_refresh(&runtime->heat_refresh);
    return;
  }

  lm_ctrl_machine_link_get_heat_info(&heat_info);

  if (!heat_info.available || !heat_info.heating) {
    reset_heat_state(&runtime->heat_state);
    clear_heat_refresh(&runtime->heat_refresh);
    return;
  }

  runtime->heat_state.session.heating = heat_info.heating;
  if (heat_info.eta_available && heat_info.ready_epoch_ms > 0) {
    have_machine_eta = sync_heat_state_deadline(
      &runtime->heat_state,
      heat_info.observed_epoch_ms,
      heat_info.ready_epoch_ms
    );
  } else {
    have_machine_eta = lm_ctrl_heat_session_has_eta(&runtime->heat_state.session);
  }
  if (have_machine_eta) {
    clear_heat_refresh(&runtime->heat_refresh);
  } else if (runtime->heat_refresh.until_us == 0) {
    arm_heat_refresh(&runtime->heat_refresh, 0);
  }
}

static bool approx_equal(float a, float b) {
  float delta = a - b;
  if (delta < 0.0f) {
    delta = -delta;
  }
  return delta < 0.05f;
}

/* Ordered pages navigated by the encoder on the main screen.
 * Backflush is swipe-only and intentionally excluded. */
static const ctrl_focus_t k_page_order[] = {
  CTRL_FOCUS_DASHBOARD,
  CTRL_FOCUS_TEMPERATURE,
  CTRL_FOCUS_PREBREW,
  CTRL_FOCUS_STEAM,
  CTRL_FOCUS_STANDBY,
  CTRL_FOCUS_BBW_MODE,
  CTRL_FOCUS_BBW_DOSE_1,
  CTRL_FOCUS_BBW_DOSE_2,
};
#define K_PAGE_ORDER_COUNT ((int)(sizeof(k_page_order) / sizeof(k_page_order[0])))

static ctrl_focus_t runtime_next_page_focus(const ctrl_state_t *state, int delta) {
  ctrl_focus_t pages[K_PAGE_ORDER_COUNT];
  int count = 0;
  int cur = 0;
  int i;

  for (i = 0; i < K_PAGE_ORDER_COUNT; i++) {
    const ctrl_focus_t f = k_page_order[i];
    if (f == CTRL_FOCUS_BBW_MODE || f == CTRL_FOCUS_BBW_DOSE_1 || f == CTRL_FOCUS_BBW_DOSE_2) {
      if ((state->feature_mask & CTRL_FEATURE_BBW) == 0) {
        continue;
      }
    }
    if (f == state->focus) {
      cur = count;
    }
    pages[count++] = f;
  }
  if (count == 0) {
    return CTRL_FOCUS_DASHBOARD;
  }
  return pages[((cur + delta) % count + count) % count];
}

static bool should_defer_machine_send(uint32_t field_mask) {
  const uint32_t deferred_mask =
    LM_CTRL_MACHINE_FIELD_TEMPERATURE |
    LM_CTRL_MACHINE_FIELD_INFUSE |
    LM_CTRL_MACHINE_FIELD_PAUSE |
    LM_CTRL_MACHINE_FIELD_BBW_DOSE_1 |
    LM_CTRL_MACHINE_FIELD_BBW_DOSE_2;

  return field_mask != LM_CTRL_MACHINE_FIELD_NONE &&
         (field_mask & deferred_mask) != 0 &&
         (field_mask & ~deferred_mask) == 0;
}

static bool has_local_ble_binding(lm_ctrl_machine_binding_t *binding) {
  lm_ctrl_machine_binding_t local_binding = {0};

  if (binding == NULL) {
    binding = &local_binding;
  }

  return lm_ctrl_wifi_get_machine_binding(binding) &&
         binding->configured &&
         binding->serial[0] != '\0' &&
         binding->communication_key[0] != '\0';
}

static void note_local_value_hold(
  lm_ctrl_runtime_local_value_hold_t *hold,
  const ctrl_values_t *values,
  uint32_t field_mask
) {
  if (hold == NULL || values == NULL || field_mask == LM_CTRL_MACHINE_FIELD_NONE) {
    return;
  }

  hold->values = *values;
  hold->mask |= field_mask;
  hold->expires_us = esp_timer_get_time() + LOCAL_VALUE_HOLD_US;
}

static void clear_delayed_machine_send_mask(
  lm_ctrl_runtime_delayed_machine_send_t *delayed_send,
  uint32_t field_mask
) {
  if (delayed_send == NULL || field_mask == LM_CTRL_MACHINE_FIELD_NONE) {
    return;
  }

  delayed_send->mask &= ~field_mask;
  if (delayed_send->mask == 0) {
    delayed_send->due_us = 0;
  }
}

static bool should_keep_local_float(
  float current_network,
  float desired_local,
  uint32_t field_mask,
  const lm_ctrl_runtime_local_value_hold_t *hold,
  int64_t now_us
) {
  if (hold == NULL || (hold->mask & field_mask) == 0 || now_us >= hold->expires_us) {
    return false;
  }
  return !approx_equal(current_network, desired_local);
}

static bool should_keep_local_bool(
  bool current_network,
  bool desired_local,
  uint32_t field_mask,
  const lm_ctrl_runtime_local_value_hold_t *hold,
  int64_t now_us
) {
  if (hold == NULL || (hold->mask & field_mask) == 0 || now_us >= hold->expires_us) {
    return false;
  }
  return current_network != desired_local;
}

static bool should_keep_local_int(
  int current_network,
  int desired_local,
  uint32_t field_mask,
  const lm_ctrl_runtime_local_value_hold_t *hold,
  int64_t now_us
) {
  if (hold == NULL || (hold->mask & field_mask) == 0 || now_us >= hold->expires_us) {
    return false;
  }
  return current_network != desired_local;
}

static void reconcile_local_value_hold(
  lm_ctrl_runtime_local_value_hold_t *hold,
  const ctrl_values_t *values,
  uint32_t loaded_mask
) {
  if (hold == NULL || hold->mask == 0 || values == NULL || loaded_mask == 0) {
    return;
  }

  if ((hold->mask & LM_CTRL_MACHINE_FIELD_TEMPERATURE) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_TEMPERATURE) != 0 &&
      approx_equal(values->temperature_c, hold->values.temperature_c)) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_TEMPERATURE;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_INFUSE) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_INFUSE) != 0 &&
      approx_equal(values->infuse_s, hold->values.infuse_s)) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_INFUSE;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_PAUSE) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_PAUSE) != 0 &&
      approx_equal(values->pause_s, hold->values.pause_s)) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_PAUSE;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_STEAM) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_STEAM) != 0 &&
      values->steam_level == hold->values.steam_level) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_STEAM;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_STANDBY) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_STANDBY) != 0 &&
      values->standby_on == hold->values.standby_on) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_STANDBY;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_BBW_MODE) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_MODE) != 0 &&
      values->bbw_mode == hold->values.bbw_mode) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_BBW_MODE;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_1) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_1) != 0 &&
      approx_equal(values->bbw_dose_1_g, hold->values.bbw_dose_1_g)) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_BBW_DOSE_1;
  }
  if ((hold->mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_2) != 0 &&
      (loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_2) != 0 &&
      approx_equal(values->bbw_dose_2_g, hold->values.bbw_dose_2_g)) {
    hold->mask &= ~LM_CTRL_MACHINE_FIELD_BBW_DOSE_2;
  }
}

static void merge_loaded_values(
  ctrl_state_t *state,
  lm_ctrl_runtime_local_value_hold_t *hold
) {
  ctrl_values_t values;
  uint32_t loaded_mask = 0;
  uint32_t feature_mask = 0;
  const int64_t now_us = esp_timer_get_time();

  if (state == NULL || !lm_ctrl_machine_link_get_values(&values, &loaded_mask, &feature_mask)) {
    return;
  }

  reconcile_local_value_hold(hold, &values, loaded_mask);

  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_TEMPERATURE) != 0 &&
      !should_keep_local_float(values.temperature_c, state->values.temperature_c, LM_CTRL_MACHINE_FIELD_TEMPERATURE, hold, now_us)) {
    state->values.temperature_c = values.temperature_c;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_INFUSE) != 0 &&
      !should_keep_local_float(values.infuse_s, state->values.infuse_s, LM_CTRL_MACHINE_FIELD_INFUSE, hold, now_us)) {
    state->values.infuse_s = values.infuse_s;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_PAUSE) != 0 &&
      !should_keep_local_float(values.pause_s, state->values.pause_s, LM_CTRL_MACHINE_FIELD_PAUSE, hold, now_us)) {
    state->values.pause_s = values.pause_s;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_STEAM) != 0 &&
      !should_keep_local_int((int)values.steam_level, (int)state->values.steam_level, LM_CTRL_MACHINE_FIELD_STEAM, hold, now_us)) {
    state->values.steam_level = values.steam_level;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_STANDBY) != 0 &&
      !should_keep_local_bool(values.standby_on, state->values.standby_on, LM_CTRL_MACHINE_FIELD_STANDBY, hold, now_us)) {
    state->values.standby_on = values.standby_on;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_MODE) != 0 &&
      !should_keep_local_int((int)values.bbw_mode, (int)state->values.bbw_mode, LM_CTRL_MACHINE_FIELD_BBW_MODE, hold, now_us)) {
    state->values.bbw_mode = values.bbw_mode;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_1) != 0 &&
      !should_keep_local_float(values.bbw_dose_1_g, state->values.bbw_dose_1_g, LM_CTRL_MACHINE_FIELD_BBW_DOSE_1, hold, now_us)) {
    state->values.bbw_dose_1_g = values.bbw_dose_1_g;
  }
  if ((loaded_mask & LM_CTRL_MACHINE_FIELD_BBW_DOSE_2) != 0 &&
      !should_keep_local_float(values.bbw_dose_2_g, state->values.bbw_dose_2_g, LM_CTRL_MACHINE_FIELD_BBW_DOSE_2, hold, now_us)) {
    state->values.bbw_dose_2_g = values.bbw_dose_2_g;
  }
  state->feature_mask = feature_mask;
  state->loaded_mask |= loaded_mask;
  /* Only reset focus when a BBW-specific page is active and BBW is no longer available.
   * MUST NOT use >= comparison — DASHBOARD and PREBREW sit above BBW in the enum. */
  if ((state->feature_mask & CTRL_FEATURE_BBW) == 0 &&
      (state->focus == CTRL_FOCUS_BBW_MODE ||
       state->focus == CTRL_FOCUS_BBW_DOSE_1 ||
       state->focus == CTRL_FOCUS_BBW_DOSE_2)) {
    state->focus = CTRL_FOCUS_DASHBOARD;
  }
}

static void maybe_flush_delayed_machine_send(
  const ctrl_state_t *state,
  lm_ctrl_runtime_delayed_machine_send_t *delayed_send,
  lm_ctrl_runtime_local_value_hold_t *hold
) {
  uint32_t field_mask;
  const int64_t now_us = esp_timer_get_time();

  if (state == NULL || delayed_send == NULL || delayed_send->mask == 0 || delayed_send->due_us == 0) {
    return;
  }
  if (now_us < delayed_send->due_us) {
    return;
  }

  field_mask = delayed_send->mask;
  if (lm_ctrl_machine_link_queue_values(&state->values, field_mask) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to queue delayed machine update fields=0x%02x", (unsigned)field_mask);
    delayed_send->due_us = now_us + (250LL * 1000LL);
    return;
  }

  note_local_value_hold(hold, &state->values, field_mask);
  delayed_send->mask = 0;
  delayed_send->due_us = 0;
}

static void log_state(const ctrl_state_t *state) {
  ESP_LOGI(
    TAG,
    "screen=%s focus=%s temp=%.1f inf=%.1f pause=%.1f steam=%d standby=%d preset=%u/%u steps=%.1f/%.1f",
    ctrl_screen_name(state->screen),
    ctrl_focus_name(state->focus),
    state->values.temperature_c,
    state->values.infuse_s,
    state->values.pause_s,
    (int)state->values.steam_level,
    state->values.standby_on,
    (unsigned)state->preset_index + 1U,
    (unsigned)state->preset_count,
    (double)state->temperature_step_c,
    (double)state->time_step_s
  );
}

static void set_status_from_action(const ctrl_state_t *state, ctrl_action_t action, char *status, size_t status_size) {
  lm_ctrl_wifi_info_t wifi_info = {0};
  ctrl_language_t language = CTRL_LANGUAGE_EN;

  if (state != NULL && state->screen == CTRL_SCREEN_SETUP) {
    lm_ctrl_wifi_format_status(status, status_size);
    return;
  }

  lm_ctrl_wifi_get_info(&wifi_info);
  language = wifi_info.language;

  switch (action.type) {
    case CTRL_ACTION_APPLY_FIELD:
      snprintf(
        status,
        status_size,
        ctrl_text(CTRL_TEXT_STATUS_FIELD_UPDATED_FMT, language),
        ctrl_focus_name_for_language(action.applied_focus, language)
      );
      break;
    case CTRL_ACTION_LOAD_PRESET:
      snprintf(
        status,
        status_size,
        ctrl_text(CTRL_TEXT_STATUS_PRESET_LOADED_FMT, language),
        action.preset_slot + 1
      );
      break;
    case CTRL_ACTION_SAVE_PRESET:
      snprintf(
        status,
        status_size,
        ctrl_text(CTRL_TEXT_STATUS_PRESET_SAVED_FMT, language),
        action.preset_slot + 1
      );
      break;
    case CTRL_ACTION_OPEN_SETUP:
      snprintf(status, status_size, "%s", ctrl_text(CTRL_TEXT_STATUS_SETUP_LOADING, language));
      break;
    case CTRL_ACTION_CLEAR_WEB_PASSWORD:
      snprintf(status, status_size, "%s", ctrl_text(CTRL_TEXT_STATUS_CLEAR_WEB_PASSWORD, language));
      break;
    case CTRL_ACTION_RESET_NETWORK:
      snprintf(status, status_size, "%s", ctrl_text(CTRL_TEXT_STATUS_RESET_NETWORK, language));
      break;
    case CTRL_ACTION_NONE:
    default:
      status[0] = '\0';
      break;
  }
}

static lm_ctrl_led_status_t led_status_from_connectivity(
  const lm_ctrl_wifi_info_t *wifi_info,
  const lm_ctrl_machine_link_info_t *machine_info
) {
  if (wifi_info == NULL || machine_info == NULL) {
    return LM_CTRL_LED_STATUS_IDLE;
  }

  if (wifi_info->portal_running && !wifi_info->sta_connected) {
    return LM_CTRL_LED_STATUS_SETUP;
  }
  if (wifi_info->sta_connecting ||
      machine_info->pending_work ||
      (machine_info->connected && !machine_info->authenticated)) {
    return LM_CTRL_LED_STATUS_CONNECTING;
  }
  if (wifi_info->cloud_connected || machine_info->authenticated || wifi_info->sta_connected) {
    return LM_CTRL_LED_STATUS_CONNECTED;
  }
  return LM_CTRL_LED_STATUS_IDLE;
}

static void sync_led_status_from_connectivity(void) {
  lm_ctrl_wifi_info_t wifi_info;
  lm_ctrl_machine_link_info_t machine_info = {0};

  lm_ctrl_wifi_get_info(&wifi_info);
  lm_ctrl_machine_link_get_info(&machine_info);
  (void)lm_ctrl_leds_set_status(led_status_from_connectivity(&wifi_info, &machine_info));
}

static void maybe_request_cloud_probe(int64_t *last_request_us) {
  lm_ctrl_wifi_info_t wifi_info;
  const int64_t now_us = esp_timer_get_time();
  esp_err_t probe_ret = ESP_ERR_INVALID_STATE;
  esp_err_t live_ret = ESP_ERR_INVALID_STATE;

  if (last_request_us == NULL) {
    return;
  }

  lm_ctrl_wifi_get_info(&wifi_info);
  if (!wifi_info.sta_connected || !wifi_info.has_cloud_credentials) {
    *last_request_us = 0;
    return;
  }

  if (*last_request_us != 0 && (now_us - *last_request_us) < CLOUD_PROBE_INTERVAL_US) {
    return;
  }

  probe_ret = lm_ctrl_wifi_request_cloud_probe();
  live_ret = lm_ctrl_wifi_request_live_updates();
  if (probe_ret == ESP_OK || live_ret == ESP_OK) {
    *last_request_us = now_us;
  }
}

static void maybe_request_value_sync(const ctrl_state_t *state) {
  lm_ctrl_wifi_info_t wifi_info;
  lm_ctrl_machine_link_info_t machine_info = {0};
  lm_ctrl_machine_binding_t binding = {0};
  uint32_t wanted_mask;
  uint32_t sync_flags = LM_CTRL_MACHINE_SYNC_NONE;
  bool local_ble_available = false;

  if (state == NULL) {
    return;
  }

  wanted_mask = LM_CTRL_MACHINE_FIELD_TEMPERATURE |
                LM_CTRL_MACHINE_FIELD_STEAM |
                LM_CTRL_MACHINE_FIELD_STANDBY |
                LM_CTRL_MACHINE_FIELD_PREBREWING;
  if ((state->feature_mask & CTRL_FEATURE_BBW) != 0) {
    wanted_mask |= LM_CTRL_MACHINE_FIELD_BBW;
  }
  if ((state->loaded_mask & wanted_mask) == wanted_mask) {
    return;
  }

  lm_ctrl_wifi_get_info(&wifi_info);
  lm_ctrl_machine_link_get_info(&machine_info);
  local_ble_available = has_local_ble_binding(&binding);
  if (machine_info.sync_pending) {
    return;
  }

  if (local_ble_available || machine_info.authenticated) {
    sync_flags |= LM_CTRL_MACHINE_SYNC_BLE;
  }
  if (wifi_info.cloud_connected && wifi_info.has_machine_selection) {
    sync_flags |= LM_CTRL_MACHINE_SYNC_CLOUD;
  }
  if (sync_flags == LM_CTRL_MACHINE_SYNC_NONE) {
    return;
  }

  if (lm_ctrl_machine_link_request_sync_mode(sync_flags) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to request machine value sync");
  }
}

static void maybe_request_periodic_value_refresh(int64_t *last_ble_request_us, int64_t *last_cloud_request_us) {
  lm_ctrl_wifi_info_t wifi_info;
  lm_ctrl_machine_link_info_t machine_info = {0};
  lm_ctrl_machine_binding_t binding = {0};
  const int64_t now_us = esp_timer_get_time();
  bool local_ble_available;
  bool can_refresh_ble;
  bool can_refresh_cloud;

  if (last_ble_request_us == NULL || last_cloud_request_us == NULL) {
    return;
  }

  lm_ctrl_wifi_get_info(&wifi_info);
  lm_ctrl_machine_link_get_info(&machine_info);
  local_ble_available = has_local_ble_binding(&binding);
  can_refresh_ble = lm_ctrl_machine_should_request_periodic_ble_sync(
    local_ble_available,
    machine_info.authenticated
  );
  can_refresh_cloud = wifi_info.cloud_connected && wifi_info.has_machine_selection;

  if (!can_refresh_ble) {
    *last_ble_request_us = 0;
  } else if (*last_ble_request_us == 0 || (now_us - *last_ble_request_us) >= BLE_VALUE_REFRESH_INTERVAL_US) {
    if (lm_ctrl_machine_link_request_sync_mode(LM_CTRL_MACHINE_SYNC_BLE) == ESP_OK) {
      *last_ble_request_us = now_us;
    } else {
      ESP_LOGW(TAG, "Failed to request periodic BLE refresh");
    }
  }

  if (!can_refresh_cloud) {
    *last_cloud_request_us = 0;
  } else if (*last_cloud_request_us == 0 || (now_us - *last_cloud_request_us) >= CLOUD_VALUE_REFRESH_INTERVAL_US) {
    if (lm_ctrl_machine_link_request_sync_mode(LM_CTRL_MACHINE_SYNC_CLOUD) == ESP_OK) {
      *last_cloud_request_us = now_us;
    } else {
      ESP_LOGW(TAG, "Failed to request periodic cloud refresh");
    }
  }
}

static void maybe_request_fast_heat_refresh(lm_ctrl_runtime_t *runtime) {
  lm_ctrl_wifi_info_t wifi_info;
  lm_ctrl_machine_link_info_t machine_info = {0};
  const int64_t now_us = esp_timer_get_time();

  if (runtime == NULL) {
    return;
  }
  lm_ctrl_wifi_get_info(&wifi_info);
  if (!wifi_info.heat_display_enabled) {
    reset_heat_state(&runtime->heat_state);
    clear_heat_refresh(&runtime->heat_refresh);
    return;
  }
  if (runtime->state.values.standby_on || lm_ctrl_heat_session_has_eta(&runtime->heat_state.session)) {
    clear_heat_refresh(&runtime->heat_refresh);
    return;
  }
  if (runtime->heat_refresh.until_us == 0) {
    return;
  }
  if (now_us >= runtime->heat_refresh.until_us) {
    clear_heat_refresh(&runtime->heat_refresh);
    return;
  }
  if (runtime->heat_refresh.next_request_us != 0 && now_us < runtime->heat_refresh.next_request_us) {
    return;
  }

  lm_ctrl_machine_link_get_info(&machine_info);
  if (!wifi_info.cloud_connected || !wifi_info.has_machine_selection || machine_info.sync_pending) {
    return;
  }

  if (lm_ctrl_machine_link_request_sync_mode(LM_CTRL_MACHINE_SYNC_CLOUD) == ESP_OK) {
    runtime->heat_refresh.next_request_us = now_us + HEAT_REFRESH_INTERVAL_US;
  }
}

/** Copy back the single field that was being edited from pre_edit_values,
 *  clear the local hold for that field, and cancel any delayed send. */
static void revert_pending_edit(lm_ctrl_runtime_t *runtime) {
  uint32_t field_mask;

  if (runtime == NULL) {
    return;
  }

  switch (runtime->pending_edit_focus) {
    case CTRL_FOCUS_TEMPERATURE:
      runtime->state.values.temperature_c = runtime->pre_edit_values.temperature_c;
      break;
    case CTRL_FOCUS_INFUSE:
      runtime->state.values.infuse_s = runtime->pre_edit_values.infuse_s;
      break;
    case CTRL_FOCUS_PAUSE:
      runtime->state.values.pause_s = runtime->pre_edit_values.pause_s;
      break;
    case CTRL_FOCUS_STEAM:
      runtime->state.values.steam_level = runtime->pre_edit_values.steam_level;
      break;
    case CTRL_FOCUS_STANDBY:
      runtime->state.values.standby_on = runtime->pre_edit_values.standby_on;
      break;
    case CTRL_FOCUS_BBW_MODE:
      runtime->state.values.bbw_mode = runtime->pre_edit_values.bbw_mode;
      break;
    case CTRL_FOCUS_BBW_DOSE_1:
      runtime->state.values.bbw_dose_1_g = runtime->pre_edit_values.bbw_dose_1_g;
      break;
    case CTRL_FOCUS_BBW_DOSE_2:
      runtime->state.values.bbw_dose_2_g = runtime->pre_edit_values.bbw_dose_2_g;
      break;
    default:
      break;
  }

  field_mask = lm_ctrl_machine_field_for_focus(runtime->pending_edit_focus);
  if (field_mask != LM_CTRL_MACHINE_FIELD_NONE) {
    runtime->local_value_hold.mask &= ~field_mask;
    clear_delayed_machine_send_mask(&runtime->delayed_machine_send, field_mask);
  }

  ESP_LOGI(TAG, "Pending edit reverted for focus=%s", ctrl_focus_name(runtime->pending_edit_focus));
}

/** Commit a pending edit: queue the current local value to the machine link,
 *  refresh the local hold, and clear the pending state. */
static void confirm_pending_edit(lm_ctrl_runtime_t *runtime) {
  uint32_t field_mask;
  ctrl_focus_t confirmed_focus;

  if (runtime == NULL || !runtime->pending_edit) {
    return;
  }

  confirmed_focus = runtime->pending_edit_focus;
  field_mask = lm_ctrl_machine_field_for_focus(confirmed_focus);
  if (field_mask != LM_CTRL_MACHINE_FIELD_NONE) {
    if (lm_ctrl_machine_link_queue_values(&runtime->state.values, field_mask) == ESP_OK) {
      note_local_value_hold(&runtime->local_value_hold, &runtime->state.values, field_mask);
      clear_delayed_machine_send_mask(&runtime->delayed_machine_send, field_mask);
      ESP_LOGI(TAG, "Pending edit confirmed for focus=%s", ctrl_focus_name(confirmed_focus));
    } else {
      ESP_LOGW(TAG, "Failed to queue confirmed edit for focus=%s", ctrl_focus_name(confirmed_focus));
    }
  }

  runtime->pending_edit = false;
  runtime->pending_edit_timeout_us = 0;

  /* Standby heat state side-effects — apply after queueing so the machine
   * link has the new value before we reset/arm the heat session. */
  if (confirmed_focus == CTRL_FOCUS_STANDBY) {
    if (runtime->state.values.standby_on) {
      reset_heat_state(&runtime->heat_state);
      clear_heat_refresh(&runtime->heat_refresh);
    } else {
      reset_heat_state(&runtime->heat_state);
      arm_heat_refresh(&runtime->heat_refresh, HEAT_REFRESH_INITIAL_DELAY_US);
    }
  }
}

void lm_ctrl_runtime_init(lm_ctrl_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

  memset(runtime, 0, sizeof(*runtime));
  reset_heat_state(&runtime->heat_state);
  reset_shot_timer_state(&runtime->shot_timer_state);
  ctrl_state_init(&runtime->state);
  if (ctrl_state_load(&runtime->state) != ESP_OK) {
    ESP_LOGW(TAG, "Falling back to default controller values");
  }
  set_status_from_action(&runtime->state, (ctrl_action_t){.type = CTRL_ACTION_NONE}, runtime->status, sizeof(runtime->status));
  log_state(&runtime->state);
}

void lm_ctrl_runtime_bootstrap(lm_ctrl_runtime_t *runtime) {
  lm_ctrl_wifi_info_t wifi_info;

  if (runtime == NULL) {
    return;
  }

  lm_ctrl_wifi_get_info(&wifi_info);
  if (!wifi_info.has_credentials) {
    ctrl_open_setup(&runtime->state);
    if (!wifi_info.portal_running) {
      (void)lm_ctrl_wifi_start_portal();
    }
    lm_ctrl_wifi_format_status(runtime->status, sizeof(runtime->status));
  }

  runtime->last_wifi_status_version = lm_ctrl_wifi_status_version();
  runtime->last_power_status_version = lm_ctrl_power_status_version();
  runtime->last_machine_status_version = lm_ctrl_machine_link_status_version();
  runtime->last_preset_version = ctrl_state_preset_version();
  maybe_request_cloud_probe(&runtime->last_cloud_probe_request_us);
  maybe_request_value_sync(&runtime->state);
  maybe_request_periodic_value_refresh(&runtime->last_ble_refresh_request_us, &runtime->last_cloud_refresh_request_us);
  sync_heat_state(runtime);
  sync_led_status_from_connectivity();
}

void lm_ctrl_runtime_handle_input_event(
  lm_ctrl_runtime_t *runtime,
  const lm_ctrl_input_event_t *event,
  bool *needs_render
) {
  ctrl_action_t action = {
    .type = CTRL_ACTION_NONE,
    .applied_focus = CTRL_FOCUS_TEMPERATURE,
    .preset_slot = -1,
  };
  lm_ctrl_wifi_info_t wifi_info = {0};
  lm_ctrl_machine_link_info_t machine_info = {0};
  lm_ctrl_controller_access_t access = {0};
  bool should_persist_state = false;
  bool preserve_status = false;

  if (runtime == NULL || event == NULL) {
    return;
  }

  action.applied_focus = runtime->state.focus;
  lm_ctrl_wifi_get_info(&wifi_info);
  lm_ctrl_machine_link_get_info(&machine_info);
  lm_ctrl_controller_compute_access(&wifi_info, &machine_info, runtime->state.feature_mask, &access);

  switch (event->type) {
    case LM_CTRL_EVENT_ROTATE:
      /* Select mode: encoder cycles between fields on multi-field pages */
      if (runtime->select_mode_active && !runtime->pending_edit) {
        runtime->select_field_index = (uint8_t)(
          ((int)runtime->select_field_index + (event->delta_steps > 0 ? 1 : -1) + 2) % 2);
        /* Extend select timeout so the user has time to press the button */
        runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
        (void)lm_ctrl_leds_indicate_rotation(event->delta_steps);
        (void)lm_ctrl_haptic_click();
        break;
      }
      /* Edit mode: encoder changes the value currently being edited */
      if (runtime->pending_edit && runtime->state.screen == CTRL_SCREEN_MAIN) {
        const ctrl_focus_t edit_focus = runtime->pending_edit_focus;
        const uint32_t field_mask = lm_ctrl_machine_field_for_focus(edit_focus);
        if (lm_ctrl_controller_field_is_editable(access.editable_mask, edit_focus) &&
            field_mask != LM_CTRL_MACHINE_FIELD_NONE) {
          /* Temporarily set focus so ctrl_rotate touches the right field */
          const ctrl_focus_t saved = runtime->state.focus;
          runtime->state.focus = edit_focus;
          ctrl_rotate(&runtime->state, event->delta_steps);
          runtime->state.focus = saved;
          note_local_value_hold(&runtime->local_value_hold, &runtime->state.values, field_mask);
          runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
        }
        (void)lm_ctrl_leds_indicate_rotation(event->delta_steps);
        (void)lm_ctrl_haptic_click();
        break;
      }
      /* Navigation mode: encoder moves between pages on the main screen */
      if (runtime->state.screen == CTRL_SCREEN_MAIN) {
        ctrl_set_focus(&runtime->state, runtime_next_page_focus(
          &runtime->state, event->delta_steps > 0 ? 1 : -1));
        (void)lm_ctrl_leds_indicate_rotation(event->delta_steps);
        (void)lm_ctrl_haptic_click();
        break;
      }
      /* Other screens (presets selector, reset arc) — use ctrl_rotate directly */
      ctrl_rotate(&runtime->state, event->delta_steps);
      (void)lm_ctrl_leds_indicate_rotation(event->delta_steps);
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_SELECT_FOCUS:
      ctrl_set_focus(&runtime->state, event->focus);
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CONFIRM_VALUE:
      if (runtime->pending_edit) {
        confirm_pending_edit(runtime);
        (void)lm_ctrl_haptic_click();
      }
      break;
    case LM_CTRL_EVENT_TOGGLE_FOCUS: {
      /* Dashboard is read-only — button press does nothing */
      if (runtime->state.screen == CTRL_SCREEN_MAIN &&
          runtime->state.focus == CTRL_FOCUS_DASHBOARD) {
        break;
      }

      /* Pre-brew combined page — 3-state click model */
      if (runtime->state.screen == CTRL_SCREEN_MAIN &&
          runtime->state.focus == CTRL_FOCUS_PREBREW) {
        if (runtime->pending_edit) {
          /* State 3: edit → confirm, return to select mode */
          confirm_pending_edit(runtime);
          runtime->select_mode_active = true;
          runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
          ESP_LOGI(TAG, "Prebrew edit confirmed — back to select mode");
          (void)lm_ctrl_haptic_click();
          break;
        }
        if (runtime->select_mode_active) {
          /* State 2: select → edit */
          const ctrl_focus_t field = runtime->select_field_index == 0
            ? CTRL_FOCUS_INFUSE : CTRL_FOCUS_PAUSE;
          if (!lm_ctrl_controller_field_is_editable(access.editable_mask, field)) {
            break;
          }
          runtime->pre_edit_values = runtime->state.values;
          runtime->pending_edit_focus = field;
          runtime->pending_edit = true;
          runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
          {
            const uint32_t fm = lm_ctrl_machine_field_for_focus(field);
            if (fm != LM_CTRL_MACHINE_FIELD_NONE) {
              note_local_value_hold(&runtime->local_value_hold, &runtime->state.values, fm);
            }
          }
          ESP_LOGI(TAG, "Prebrew: enter edit for field=%s", ctrl_focus_name(field));
          (void)lm_ctrl_haptic_click();
          break;
        }
        /* State 1: view → select */
        runtime->select_mode_active = true;
        runtime->select_field_index = 0;
        runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
        ESP_LOGI(TAG, "Prebrew: enter select mode");
        (void)lm_ctrl_haptic_click();
        break;
      }

      /* Single-value pages — 2-state model: view → edit → confirm */
      {
        const ctrl_focus_t eff_focus = runtime->state.focus;

        if (runtime->state.screen == CTRL_SCREEN_MAIN &&
            !lm_ctrl_controller_field_is_editable(access.editable_mask, eff_focus)) {
          break;
        }
        if (runtime->pending_edit && runtime->pending_edit_focus == eff_focus) {
          /* Edit → confirm */
          const bool waking_from_standby =
            eff_focus == CTRL_FOCUS_STANDBY &&
            runtime->state.values.standby_on == false;
          confirm_pending_edit(runtime);
          if (eff_focus == CTRL_FOCUS_STANDBY && runtime->state.values.standby_on) {
            reset_heat_state(&runtime->heat_state);
            clear_heat_refresh(&runtime->heat_refresh);
          } else if (waking_from_standby) {
            reset_heat_state(&runtime->heat_state);
            arm_heat_refresh(&runtime->heat_refresh, HEAT_REFRESH_INITIAL_DELAY_US);
          }
          (void)lm_ctrl_haptic_click();
          break;
        }
        if (runtime->pending_edit) {
          confirm_pending_edit(runtime);
        }
        /* View → edit */
        runtime->pre_edit_values = runtime->state.values;
        ctrl_toggle_focus(&runtime->state, eff_focus);
        runtime->pending_edit_focus = eff_focus;
        runtime->pending_edit = true;
        runtime->pending_edit_timeout_us = esp_timer_get_time() + PENDING_EDIT_TIMEOUT_US;
        {
          const uint32_t field_mask = lm_ctrl_machine_field_for_focus(eff_focus);
          if (field_mask != LM_CTRL_MACHINE_FIELD_NONE) {
            note_local_value_hold(&runtime->local_value_hold, &runtime->state.values, field_mask);
          }
        }
        ESP_LOGI(TAG, "Edit start focus=%s", ctrl_focus_name(eff_focus));
        (void)lm_ctrl_haptic_click();
      }
      break;
    }
    case LM_CTRL_EVENT_OPEN_PRESETS:
      ctrl_open_presets(&runtime->state);
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_LOAD_PRESET:
      if (!access.preset_load_enabled) {
        break;
      }
      action = ctrl_load_preset(&runtime->state);
      if (action.type == CTRL_ACTION_LOAD_PRESET) {
        const uint32_t field_mask = lm_ctrl_machine_preset_field_mask(runtime->state.feature_mask);
        if (lm_ctrl_machine_link_queue_values(&runtime->state.values, field_mask) != ESP_OK) {
          ESP_LOGW(TAG, "Failed to queue BLE preset sync");
        } else {
          note_local_value_hold(&runtime->local_value_hold, &runtime->state.values, field_mask);
          clear_delayed_machine_send_mask(&runtime->delayed_machine_send, field_mask);
        }
      }
      should_persist_state = action.type == CTRL_ACTION_LOAD_PRESET;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_SAVE_PRESET:
      action = ctrl_save_preset(&runtime->state);
      should_persist_state = action.type == CTRL_ACTION_SAVE_PRESET;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_OPEN_SETUP:
      ctrl_open_setup(&runtime->state);
      if (lm_ctrl_wifi_start_portal() != ESP_OK) {
        snprintf(runtime->status, sizeof(runtime->status), "Could not start the setup portal.");
        preserve_status = true;
      }
      sync_led_status_from_connectivity();
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_OPEN_SETUP_RESET:
      ctrl_open_setup_reset(&runtime->state);
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CANCEL_SETUP_RESET:
      ctrl_cancel_setup_reset(&runtime->state);
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CONFIRM_SETUP_RESET:
      action = ctrl_confirm_setup_reset(&runtime->state);
      if (action.type == CTRL_ACTION_CLEAR_WEB_PASSWORD) {
        if (lm_ctrl_wifi_clear_web_admin_password() != ESP_OK) {
          snprintf(runtime->status, sizeof(runtime->status), "Could not clear the web password.");
          action.type = CTRL_ACTION_NONE;
          preserve_status = true;
        }
      } else if (action.type == CTRL_ACTION_RESET_NETWORK && lm_ctrl_wifi_reset_network() != ESP_OK) {
        snprintf(runtime->status, sizeof(runtime->status), "Could not reset network settings.");
        action.type = CTRL_ACTION_NONE;
        preserve_status = true;
      }
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_DISMISS_SHOT_TIMER:
      if (lm_ctrl_shot_timer_dismiss(&runtime->shot_timer_state)) {
        (void)lm_ctrl_haptic_click();
      }
      break;
    case LM_CTRL_EVENT_OPEN_SETTINGS:
      runtime->state.screen = CTRL_SCREEN_SETTINGS;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CONNECT_MACHINE:
      if (lm_ctrl_machine_link_request_sync_mode(LM_CTRL_MACHINE_SYNC_ALL) == ESP_OK) {
        snprintf(runtime->status, sizeof(runtime->status), "Connecting to machine...");
      } else {
        snprintf(runtime->status, sizeof(runtime->status), "No machine selected yet.");
      }
      preserve_status = true;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_OPEN_BACKFLUSH:
      runtime->backflush_open = true;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CLOSE_BACKFLUSH:
      runtime->backflush_open = false;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_START_BACKFLUSH:
      runtime->backflush_open = false;
      if (lm_ctrl_machine_link_start_backflush() == ESP_OK) {
        snprintf(runtime->status, sizeof(runtime->status), "Backflush started.");
      } else {
        snprintf(runtime->status, sizeof(runtime->status), "Backflush command failed.");
      }
      preserve_status = true;
      (void)lm_ctrl_haptic_click();
      break;
    case LM_CTRL_EVENT_CLOSE_SCREEN:
      if (runtime->state.screen == CTRL_SCREEN_SETTINGS) {
        runtime->state.screen = CTRL_SCREEN_MAIN;
      } else {
        ctrl_close_overlay(&runtime->state);
      }
      (void)lm_ctrl_haptic_click();
      break;
    default:
      break;
  }

  if (should_persist_state && ctrl_state_persist(&runtime->state) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to persist controller state");
  }

  if (!preserve_status) {
    set_status_from_action(&runtime->state, action, runtime->status, sizeof(runtime->status));
  }
  log_state(&runtime->state);
  if (needs_render != NULL) {
    *needs_render = true;
  }
}

void lm_ctrl_runtime_handle_wifi_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  uint32_t wifi_status_version;

  if (runtime == NULL) {
    return;
  }

  wifi_status_version = lm_ctrl_wifi_status_version();
  if (wifi_status_version == runtime->last_wifi_status_version) {
    return;
  }

  runtime->last_wifi_status_version = wifi_status_version;
  sync_led_status_from_connectivity();
  maybe_request_cloud_probe(&runtime->last_cloud_probe_request_us);
  maybe_request_value_sync(&runtime->state);
  maybe_request_periodic_value_refresh(&runtime->last_ble_refresh_request_us, &runtime->last_cloud_refresh_request_us);
  sync_heat_state(runtime);
  if (runtime->state.screen == CTRL_SCREEN_SETUP) {
    lm_ctrl_wifi_format_status(runtime->status, sizeof(runtime->status));
  }
  if (needs_render != NULL) {
    *needs_render = true;
  }
}

void lm_ctrl_runtime_handle_power_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  uint32_t power_status_version;

  if (runtime == NULL) {
    return;
  }

  power_status_version = lm_ctrl_power_status_version();
  if (power_status_version == runtime->last_power_status_version) {
    return;
  }

  runtime->last_power_status_version = power_status_version;
  if (needs_render != NULL) {
    *needs_render = true;
  }
}

void lm_ctrl_runtime_handle_machine_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  uint32_t machine_status_version;
  lm_ctrl_machine_link_info_t machine_info = {0};

  if (runtime == NULL) {
    return;
  }

  machine_status_version = lm_ctrl_machine_link_status_version();
  if (machine_status_version == runtime->last_machine_status_version) {
    return;
  }

  runtime->last_machine_status_version = machine_status_version;
  sync_led_status_from_connectivity();
  merge_loaded_values(&runtime->state, &runtime->local_value_hold);
  sync_heat_state(runtime);
  lm_ctrl_machine_link_get_info(&machine_info);
  if (machine_info.heat_hint_active &&
      !lm_ctrl_heat_session_has_eta(&runtime->heat_state.session) &&
      runtime->heat_refresh.until_us == 0) {
    arm_heat_refresh(&runtime->heat_refresh, 0);
  }
  maybe_request_value_sync(&runtime->state);
  maybe_request_periodic_value_refresh(&runtime->last_ble_refresh_request_us, &runtime->last_cloud_refresh_request_us);
  if (needs_render != NULL) {
    *needs_render = true;
  }
}

void lm_ctrl_runtime_handle_preset_change(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  uint32_t preset_version;

  if (runtime == NULL) {
    return;
  }

  preset_version = ctrl_state_preset_version();
  if (preset_version == runtime->last_preset_version) {
    return;
  }

  runtime->last_preset_version = preset_version;
  if (ctrl_state_refresh_presets(&runtime->state) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to refresh preset definitions");
  } else if (needs_render != NULL) {
    *needs_render = true;
  }
}

void lm_ctrl_runtime_tick(lm_ctrl_runtime_t *runtime, bool *needs_render) {
  int32_t remaining_s;
  int32_t progress_permille;

  if (runtime == NULL) {
    return;
  }

  /* Revert a pending edit if the user has not confirmed within the timeout */
  if (runtime->pending_edit &&
      runtime->pending_edit_timeout_us > 0 &&
      esp_timer_get_time() >= runtime->pending_edit_timeout_us) {
    revert_pending_edit(runtime);
    runtime->pending_edit = false;
    runtime->pending_edit_timeout_us = 0;
    runtime->select_mode_active = false;
    runtime->select_field_index = 0;
    ESP_LOGI(TAG, "Pending edit timed out — value reverted");
    if (needs_render != NULL) {
      *needs_render = true;
    }
  }

  /* Time out select mode when user pauses before choosing to edit */
  if (!runtime->pending_edit && runtime->select_mode_active &&
      runtime->pending_edit_timeout_us > 0 &&
      esp_timer_get_time() >= runtime->pending_edit_timeout_us) {
    runtime->select_mode_active = false;
    runtime->select_field_index = 0;
    runtime->pending_edit_timeout_us = 0;
    ESP_LOGI(TAG, "Select mode timed out");
    if (needs_render != NULL) {
      *needs_render = true;
    }
  }

  maybe_request_cloud_probe(&runtime->last_cloud_probe_request_us);
  maybe_request_value_sync(&runtime->state);
  maybe_request_periodic_value_refresh(&runtime->last_ble_refresh_request_us, &runtime->last_cloud_refresh_request_us);
  maybe_request_fast_heat_refresh(runtime);
  maybe_flush_delayed_machine_send(&runtime->state, &runtime->delayed_machine_send, &runtime->local_value_hold);
  remaining_s = heat_state_remaining_seconds(&runtime->heat_state);
  progress_permille = (int32_t)heat_state_progress_permille(&runtime->heat_state);
  if (runtime->heat_state.session.heating &&
      lm_ctrl_heat_session_has_eta(&runtime->heat_state.session) &&
      (remaining_s != runtime->heat_state.last_rendered_remaining_s ||
       progress_permille != runtime->heat_state.last_rendered_progress_permille)) {
    runtime->heat_state.last_rendered_remaining_s = remaining_s;
    runtime->heat_state.last_rendered_progress_permille = progress_permille;
    if (needs_render != NULL) {
      *needs_render = true;
    }
  }
  maybe_refresh_shot_timer(runtime, needs_render);
}

const ctrl_state_t *lm_ctrl_runtime_state(const lm_ctrl_runtime_t *runtime) {
  return runtime != NULL ? &runtime->state : NULL;
}

const char *lm_ctrl_runtime_status(const lm_ctrl_runtime_t *runtime) {
  return runtime != NULL ? runtime->status : "";
}

void lm_ctrl_runtime_build_ui_view(const lm_ctrl_runtime_t *runtime, lm_ctrl_ui_view_t *view) {
  lm_ctrl_wifi_info_t wifi_info = {0};
  lm_ctrl_power_info_t power_info = {0};
  lm_ctrl_machine_link_info_t machine_info = {0};
  lm_ctrl_controller_access_t access = {0};

  if (runtime == NULL || view == NULL) {
    return;
  }

  memset(view, 0, sizeof(*view));
  lm_ctrl_wifi_get_info(&wifi_info);
  lm_ctrl_power_get_info(&power_info);
  lm_ctrl_machine_link_get_info(&machine_info);
  lm_ctrl_controller_compute_access(&wifi_info, &machine_info, runtime->state.feature_mask, &access);

  view->language = wifi_info.language;
  view->remote_path_state = access.remote_path_state;
  view->usb_visible = power_info.usb_connected;
  view->battery_visible = power_info.low || power_info.charging;
  view->battery_charging = power_info.charging;
  view->battery_low = power_info.low;
  view->heat_visible =
    wifi_info.heat_display_enabled &&
    (runtime->heat_state.session.heating || machine_info.heat_hint_active);
  view->heat_arc_visible =
    wifi_info.heat_display_enabled &&
    lm_ctrl_heat_session_has_eta(&runtime->heat_state.session) &&
    view->heat_visible;
  view->water_alert_visible = machine_info.water_status.available && machine_info.water_status.no_water;
  view->ble_visible = machine_info.connected || machine_info.authenticated;
  view->ble_authenticated = machine_info.authenticated;
  view->readable_mask = access.readable_mask;
  view->editable_mask = access.editable_mask;
  view->preset_load_enabled = access.preset_load_enabled;
  view->heat_progress_permille = heat_state_progress_permille(&runtime->heat_state);
  view->custom_logo = wifi_info.has_custom_logo ? lm_ctrl_wifi_get_custom_logo() : NULL;
  view->backflush_visible = runtime->backflush_open;
  view->pending_edit = runtime->pending_edit;
  view->pending_edit_focus = runtime->pending_edit_focus;
  view->shot_timer_visible = lm_ctrl_shot_timer_visible(&runtime->shot_timer_state);
  view->shot_timer_dismissable = lm_ctrl_shot_timer_dismissable(&runtime->shot_timer_state);
  if (view->shot_timer_visible) {
    snprintf(
      view->shot_timer_text,
      sizeof(view->shot_timer_text),
      "%u:%02u",
      (unsigned int)(runtime->shot_timer_state.seconds / 60U),
      (unsigned int)(runtime->shot_timer_state.seconds % 60U)
    );
  }
  if (view->heat_arc_visible) {
    int32_t remaining_s = heat_state_remaining_seconds(&runtime->heat_state);
    snprintf(
      view->heat_eta_text,
      sizeof(view->heat_eta_text),
      "%d:%02d",
      (int)(remaining_s / 60),
      (int)(remaining_s % 60)
    );
  }
  snprintf(view->setup_status_text, sizeof(view->setup_status_text), "%s", runtime->status);
  lm_ctrl_wifi_get_setup_qr_payload(view->setup_qr_payload, sizeof(view->setup_qr_payload));
}
