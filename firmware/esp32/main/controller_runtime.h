#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "brew_counter.h"
#include "brew_timer.h"
#include "controller_heat_session.h"
#include "controller_shot_timer.h"
#include "controller_state.h"
#include "controller_ui.h"
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  ctrl_values_t values;
  uint32_t mask;
  int64_t expires_us;
} lm_ctrl_runtime_local_value_hold_t;

typedef struct {
  uint32_t mask;
  int64_t due_us;
} lm_ctrl_runtime_delayed_machine_send_t;

typedef struct {
  lm_ctrl_heat_session_t session;
  int32_t last_rendered_remaining_s;
  int32_t last_rendered_progress_permille;
} lm_ctrl_runtime_heat_state_t;

typedef struct {
  int64_t until_us;
  int64_t next_request_us;
} lm_ctrl_runtime_heat_refresh_t;

typedef lm_ctrl_shot_timer_state_t lm_ctrl_runtime_shot_timer_state_t;

/** Runtime-owned controller state, status text, and sync bookkeeping. */
typedef struct {
  ctrl_state_t state;
  char status[256];
  uint32_t last_wifi_status_version;
  uint32_t last_power_status_version;
  uint32_t last_machine_status_version;
  uint32_t last_preset_version;
  int64_t last_cloud_probe_request_us;
  int64_t last_ble_refresh_request_us;
  int64_t last_cloud_refresh_request_us;
  lm_ctrl_runtime_local_value_hold_t local_value_hold;
  lm_ctrl_runtime_delayed_machine_send_t delayed_machine_send;
  lm_ctrl_runtime_heat_state_t heat_state;
  lm_ctrl_runtime_heat_refresh_t heat_refresh;
  lm_ctrl_runtime_shot_timer_state_t shot_timer_state;
  bool backflush_open;

  /* Pending edit — encoder rotation sets local value but does NOT send to
   * machine until the user explicitly confirms (button press, tap on value,
   * or CONFIRM_VALUE event).  If the timeout fires without confirmation the
   * local value is reverted to what the machine last reported. */
  bool pending_edit;
  ctrl_focus_t pending_edit_focus;
  ctrl_values_t pre_edit_values;    /* machine-reported values at edit start */
  int64_t pending_edit_timeout_us;  /* absolute esp_timer time; 0 = not armed */

  /* Select mode — multi-field pages (prebrew) use click to pick a field before
   * entering edit.  select_field_index: 0=first field, 1=second field. */
  bool select_mode_active;
  uint8_t select_field_index;

  lm_ctrl_brew_timer_t brew_timer;
  lm_ctrl_brew_counter_t brew_counter;
} lm_ctrl_runtime_t;

void lm_ctrl_runtime_init(lm_ctrl_runtime_t *runtime);
void lm_ctrl_runtime_bootstrap(lm_ctrl_runtime_t *runtime);
void lm_ctrl_runtime_handle_input_event(lm_ctrl_runtime_t *runtime, const lm_ctrl_input_event_t *event, bool *needs_render);
void lm_ctrl_runtime_handle_wifi_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render);
void lm_ctrl_runtime_handle_power_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render);
void lm_ctrl_runtime_handle_machine_status_change(lm_ctrl_runtime_t *runtime, bool *needs_render);
void lm_ctrl_runtime_handle_preset_change(lm_ctrl_runtime_t *runtime, bool *needs_render);
void lm_ctrl_runtime_tick(lm_ctrl_runtime_t *runtime, bool *needs_render);
const ctrl_state_t *lm_ctrl_runtime_state(const lm_ctrl_runtime_t *runtime);
const char *lm_ctrl_runtime_status(const lm_ctrl_runtime_t *runtime);
void lm_ctrl_runtime_build_ui_view(const lm_ctrl_runtime_t *runtime, lm_ctrl_ui_view_t *view);

#ifdef __cplusplus
}
#endif
