#pragma once

#include "esp_err.h"
#include "lvgl.h"

#include "controller_connectivity.h"
#include "controller_state.h"

/** Maximum number of horizontally swipeable main pages in the round UI.
 *  The last slot is always the Backflush page. */
#define LM_CTRL_UI_MAIN_PAGE_COUNT 9
/** Maximum number of touch bindings stored for button-like actions. */
#define LM_CTRL_UI_BINDING_COUNT 17
/** Maximum setup status text length passed into the UI view model. */
#define LM_CTRL_UI_STATUS_TEXT_LEN 256
/** Maximum setup QR payload length passed into the UI view model. */
#define LM_CTRL_UI_SETUP_QR_LEN 192

/** Read-only UI view model built by the runtime layer. */
typedef struct {
  ctrl_language_t language;
  lm_ctrl_remote_path_state_t remote_path_state;
  bool usb_visible;
  bool battery_visible;
  bool battery_charging;
  bool battery_low;
  bool heat_visible;
  bool heat_arc_visible;
  bool water_alert_visible;
  bool ble_visible;
  bool ble_authenticated;
  bool shot_timer_visible;
  bool shot_timer_dismissable;
  uint32_t readable_mask;
  uint32_t editable_mask;
  bool preset_load_enabled;
  uint16_t heat_progress_permille;
  const lv_img_dsc_t *custom_logo;
  bool backflush_visible;
  bool pending_edit;
  ctrl_focus_t pending_edit_focus;
  bool select_mode_active;
  uint8_t select_field_index;   /**< 0=first field (infuse), 1=second (pause) */
  char shot_timer_text[24];
  char brew_timer_text[16];     /**< formatted SS.D */
  bool brew_timer_running;
  char heat_eta_text[16];
  char setup_status_text[LM_CTRL_UI_STATUS_TEXT_LEN];
  char setup_qr_payload[LM_CTRL_UI_SETUP_QR_LEN];
} lm_ctrl_ui_view_t;

/** UI-level actions forwarded back into the controller input queue. */
typedef enum {
  LM_CTRL_UI_ACTION_SELECT_FOCUS = 0,
  LM_CTRL_UI_ACTION_TOGGLE_FOCUS,
  LM_CTRL_UI_ACTION_OPEN_PRESETS,
  LM_CTRL_UI_ACTION_LOAD_PRESET,
  LM_CTRL_UI_ACTION_SAVE_PRESET,
  LM_CTRL_UI_ACTION_OPEN_SETUP,
  LM_CTRL_UI_ACTION_CLOSE_SCREEN,
  LM_CTRL_UI_ACTION_OPEN_SETUP_RESET,
  LM_CTRL_UI_ACTION_CANCEL_SETUP_RESET,
  LM_CTRL_UI_ACTION_CONFIRM_SETUP_RESET,
  LM_CTRL_UI_ACTION_DISMISS_SHOT_TIMER,
  LM_CTRL_UI_ACTION_OPEN_BACKFLUSH,
  LM_CTRL_UI_ACTION_CLOSE_BACKFLUSH,
  LM_CTRL_UI_ACTION_START_BACKFLUSH,
  LM_CTRL_UI_ACTION_CONFIRM_VALUE,
  LM_CTRL_UI_ACTION_OPEN_SETTINGS,
  LM_CTRL_UI_ACTION_CONNECT_MACHINE,
  LM_CTRL_UI_ACTION_OPEN_BREW_TIMER,
  LM_CTRL_UI_ACTION_CLOSE_BREW_TIMER,
  LM_CTRL_UI_ACTION_TOGGLE_BREW_TIMER_RUN,
  LM_CTRL_UI_ACTION_RESET_BREW_TIMER,
} lm_ctrl_ui_action_t;

/** Callback invoked when the UI needs the main loop to handle a touch action. */
typedef void (*lm_ctrl_ui_action_cb_t)(lm_ctrl_ui_action_t action, ctrl_focus_t focus, void *user_data);

typedef struct lm_ctrl_ui_s lm_ctrl_ui_t;

/** Touch binding metadata used to map LVGL objects back to controller actions. */
typedef struct {
  lm_ctrl_ui_t *ui;
  lm_ctrl_ui_action_t action;
  ctrl_focus_t focus;
} lm_ctrl_ui_binding_t;

/** Live LVGL object tree for the round controller UI. */
struct lm_ctrl_ui_s {
  lv_obj_t *screen;
  lv_obj_t *ring;
  lv_obj_t *title_text;
  lv_obj_t *title_image;
  lv_obj_t *wifi_icon;
  lv_obj_t *wifi_slash_icon;
  lv_obj_t *usb_icon;
  lv_obj_t *battery_icon;
  lv_obj_t *heat_icon;
  lv_obj_t *water_icon;
  lv_obj_t *ble_icon;
  lv_obj_t *heat_arc;
  lv_obj_t *page_label;
  lv_obj_t *page_dots[LM_CTRL_UI_MAIN_PAGE_COUNT];

  lv_obj_t *main_card;
  lv_obj_t *focus;
  lv_obj_t *value;
  lv_obj_t *hint;
  lv_obj_t *shot_timer_card;
  lv_obj_t *shot_timer_title;
  lv_obj_t *shot_timer_value;
  lv_obj_t *power_left_button;
  lv_obj_t *power_left_title;
  lv_obj_t *power_left_value;
  lv_obj_t *power_right_button;
  lv_obj_t *power_right_title;
  lv_obj_t *power_right_value;
  lv_obj_t *power_hint;

  lv_obj_t *presets_card;
  lv_obj_t *presets_title;
  lv_obj_t *presets_name;
  lv_obj_t *presets_body;
  lv_obj_t *presets_load_button;
  lv_obj_t *presets_load_label;
  lv_obj_t *presets_save_button;
  lv_obj_t *presets_save_label;

  lv_obj_t *setup_card;
  lv_obj_t *setup_title;
  lv_obj_t *setup_qr;
  lv_obj_t *setup_body;
  lv_obj_t *setup_action_list;
  lv_obj_t *setup_reset_arc;
  lv_obj_t *setup_connect_button;
  lv_obj_t *setup_connect_label;
  lv_obj_t *setup_secondary_button;
  lv_obj_t *setup_secondary_label;
  lv_obj_t *setup_primary_button;
  lv_obj_t *setup_primary_label;

  ctrl_focus_t rendered_focus;
  ctrl_screen_t rendered_screen;
  uint32_t rendered_feature_mask;
  bool rendered_shot_timer_visible;
  bool rendered_shot_timer_dismissable;
  bool rendered_backflush_visible;

  lv_obj_t *backflush_card;
  lv_obj_t *backflush_title;
  lv_obj_t *backflush_hint;
  lv_obj_t *backflush_start_button;
  lv_obj_t *backflush_start_label;

  /* Dashboard panels (shown when focus is CTRL_FOCUS_DASHBOARD) — read-only */
  lv_obj_t *dash_clock_panel;
  lv_obj_t *dash_clock_hh;
  lv_obj_t *dash_clock_mm;
  lv_obj_t *dash_clock_hhmm;   /**< colon separator or no-sync placeholder */
  lv_obj_t *dash_clock_no_sync;
  lv_obj_t *dash_brew_count;   /**< daily shot count badge, no icon */
  lv_obj_t *dash_temp_panel;
  lv_obj_t *dash_temp_title;   /**< "Coffee" label */
  lv_obj_t *dash_temp_value;   /**< temperature value */
  lv_obj_t *dash_pb_title;     /**< "Pre-brew" label */
  lv_obj_t *dash_pb_inline;    /**< combined "3.0s · 1.5s" */
  lv_obj_t *dash_steam_title;  /**< "Steam" label */
  lv_obj_t *dash_steam_value;  /**< steam level text */

  /* Brew timer screen */
  lv_obj_t *brew_timer_card;
  lv_obj_t *brew_timer_value;
  lv_obj_t *brew_timer_hint;
  lv_obj_t *brew_timer_startstop_button;
  lv_obj_t *brew_timer_startstop_label;
  lv_obj_t *brew_timer_reset_button;
  lv_obj_t *brew_timer_reset_label;

  /* Pre-brew combined page (CTRL_FOCUS_PREBREW) */
  lv_obj_t *prebrew_card;
  lv_obj_t *prebrew_title;
  lv_obj_t *prebrew_in_title;
  lv_obj_t *prebrew_in_value;
  lv_obj_t *prebrew_out_title;
  lv_obj_t *prebrew_out_value;
  lv_obj_t *prebrew_hint;

  /* Settings screen */
  lv_obj_t *settings_card;
  lv_obj_t *settings_title;
  lv_obj_t *settings_theme_label;
  lv_obj_t *settings_theme_name;
  lv_obj_t *settings_theme_prev;
  lv_obj_t *settings_theme_prev_label;
  lv_obj_t *settings_theme_next;
  lv_obj_t *settings_theme_next_label;
  lv_obj_t *settings_bl_label;
  lv_obj_t *settings_bl_indicators[5];
  lv_obj_t *settings_bl_down;
  lv_obj_t *settings_bl_down_label;
  lv_obj_t *settings_bl_up;
  lv_obj_t *settings_bl_up_label;
  lv_obj_t *settings_reset_button;
  lv_obj_t *settings_reset_label;

  uint8_t settings_theme_index;
  uint8_t settings_backlight_level;

  lm_ctrl_ui_action_cb_t action_cb;
  void *action_user_data;
  lm_ctrl_ui_binding_t bindings[LM_CTRL_UI_BINDING_COUNT];
};

/**
 * Build the LVGL controller UI on the active display.
 *
 * The initial render uses the provided state and status text, then later updates
 * are applied through lm_ctrl_ui_render().
 */
esp_err_t lm_ctrl_ui_init(
  lm_ctrl_ui_t *ui,
  const ctrl_state_t *state,
  const lm_ctrl_ui_view_t *view,
  lm_ctrl_ui_action_cb_t action_cb,
  void *action_user_data
);
/** Refresh the UI to reflect the latest controller state and setup status text. */
void lm_ctrl_ui_render(lm_ctrl_ui_t *ui, const ctrl_state_t *state, const lm_ctrl_ui_view_t *view);
