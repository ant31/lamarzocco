#include "input.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "board_display.h"
#include "esp_lcd_touch.h"

static const char *TAG = "lm_input";

#define LM_CTRL_KNOB_POLL_INTERVAL_US   3000
#define LM_CTRL_KNOB_DEBOUNCE_TICKS     2
#define LM_CTRL_BTN_POLL_INTERVAL_US    50000     /* 50 ms */
#define LM_CTRL_BTN_DEBOUNCE_TICKS      3         /* 3 × 50 ms = 150 ms */
#define LM_CTRL_PCF8574_ADDR            0x21
#define LM_CTRL_PCF8574_BTN_BIT         5         /* P5 = push button, active LOW */
#define LM_CTRL_TOUCH_DEBUG_INTERVAL_US 2000000   /* 2 s */

static QueueHandle_t s_event_queue = NULL;
static esp_timer_handle_t s_knob_timer        = NULL;
static esp_timer_handle_t s_btn_timer         = NULL;
static esp_timer_handle_t s_touch_debug_timer = NULL;

/* Button state for debounce */
static uint8_t s_btn_pressed_ticks = 0;
static uint8_t s_btn_released_ticks = 0;
static bool    s_btn_state = false; /* true = currently pressed */

static void touch_debug_cb(void *arg) {
  (void)arg;
  esp_lcd_touch_handle_t tp = lm_ctrl_display_touch_handle();
  if (tp == NULL) {
    ESP_LOGW(TAG, "touch_debug: touch handle is NULL");
    return;
  }

  esp_err_t err = esp_lcd_touch_read_data(tp);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "touch_debug: esp_lcd_touch_read_data failed: %s", esp_err_to_name(err));
    return;
  }

  esp_lcd_touch_point_data_t point_data[5];
  uint8_t touch_cnt = 0;
  esp_lcd_touch_get_data(tp, point_data, &touch_cnt, 5);
  if (touch_cnt > 0) {
    for (uint8_t i = 0; i < touch_cnt; i++) {
      ESP_LOGI(TAG, "touch_debug[%u/%u]: x=%u y=%u strength=%u",
               i + 1u, touch_cnt,
               (unsigned)point_data[i].x,
               (unsigned)point_data[i].y,
               (unsigned)point_data[i].strength);
    }
  } else {
    ESP_LOGD(TAG, "touch_debug: no touch detected");
  }

  /* Also snapshot PCF8574 so button state is visible alongside touch */
  i2c_master_bus_handle_t bus = lm_ctrl_display_i2c_bus();
  if (bus != NULL) {
    i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address  = LM_CTRL_PCF8574_ADDR,
      .scl_speed_hz    = 100000,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
      uint8_t pcf_val = 0xFF;
      if (i2c_master_receive(dev, &pcf_val, 1, pdMS_TO_TICKS(20)) == ESP_OK) {
        ESP_LOGI(TAG, "touch_debug: PCF8574=0x%02X btn(P5)=%u",
                 pcf_val, (unsigned)((pcf_val >> LM_CTRL_PCF8574_BTN_BIT) & 0x01u));
      }
      i2c_master_bus_rm_device(dev);
    }
  }
}

static esp_err_t pcf8574_read_byte(uint8_t *out) {
  i2c_master_bus_handle_t bus = lm_ctrl_display_i2c_bus();
  if (bus == NULL) return ESP_ERR_INVALID_STATE;

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = LM_CTRL_PCF8574_ADDR,
    .scl_speed_hz    = 100000,
  };
  i2c_master_dev_handle_t dev;
  esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
  if (err != ESP_OK) return err;
  err = i2c_master_receive(dev, out, 1, pdMS_TO_TICKS(20));
  i2c_master_bus_rm_device(dev);
  return err;
}

static void btn_poll_cb(void *arg) {
  (void)arg;

  uint8_t pcf_val = 0xFF;
  if (pcf8574_read_byte(&pcf_val) != ESP_OK) return;

  bool raw_pressed = !((pcf_val >> LM_CTRL_PCF8574_BTN_BIT) & 0x01);

  if (raw_pressed) {
    s_btn_released_ticks = 0;
    if (!s_btn_state) {
      if (++s_btn_pressed_ticks >= LM_CTRL_BTN_DEBOUNCE_TICKS) {
        s_btn_state = true;
        s_btn_pressed_ticks = 0;
        if (s_event_queue != NULL) {
          const lm_ctrl_input_event_t ev = {
            .type        = LM_CTRL_EVENT_TOGGLE_FOCUS,
            .delta_steps = 0,
            .focus       = CTRL_FOCUS_TEMPERATURE,
          };
          if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Input queue full, dropping button press");
          }
          ESP_LOGD(TAG, "PCF8574 button pressed");
        }
      }
    }
  } else {
    s_btn_pressed_ticks = 0;
    if (s_btn_state) {
      if (++s_btn_released_ticks >= LM_CTRL_BTN_DEBOUNCE_TICKS) {
        s_btn_state = false;
        s_btn_released_ticks = 0;
        ESP_LOGD(TAG, "PCF8574 button released");
      }
    }
  }
}

#if LM_CTRL_ENCODER_ENABLED
static uint8_t s_encoder_a_level = 1;
static uint8_t s_encoder_b_level = 1;
static uint8_t s_debounce_a_cnt = 0;
static uint8_t s_debounce_b_cnt = 0;
static int s_count_value = 0;

static void push_event(lm_ctrl_input_event_type_t type, int delta_steps) {
  if (s_event_queue == NULL) {
    return;
  }

  const lm_ctrl_input_event_t event = {
    .type = type,
    .delta_steps = delta_steps,
    .focus = CTRL_FOCUS_TEMPERATURE,
  };
  if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Input queue full, dropping event type=%d", (int)type);
  }
}

static void process_knob_channel(uint8_t current_level, uint8_t *prev_level, uint8_t *debounce_cnt, int delta_steps) {
  if (current_level == 0) {
    if (current_level != *prev_level) {
      *debounce_cnt = 0;
    } else {
      (*debounce_cnt)++;
    }
  } else {
    if (current_level != *prev_level && ++(*debounce_cnt) >= LM_CTRL_KNOB_DEBOUNCE_TICKS) {
      *debounce_cnt = 0;
      const int adjusted_delta = delta_steps * LM_CTRL_KNOB_DIRECTION;
      s_count_value += adjusted_delta;
      push_event(LM_CTRL_EVENT_ROTATE, adjusted_delta);
      ESP_LOGD(TAG, "Ring step delta=%d count=%d levels=(%u,%u)", adjusted_delta, s_count_value, s_encoder_a_level, s_encoder_b_level);
    } else {
      *debounce_cnt = 0;
    }
  }

  *prev_level = current_level;
}

static void knob_poll_cb(void *arg) {
  (void)arg;

  const uint8_t pha_value = (uint8_t)gpio_get_level(LM_CTRL_KNOB_A);
  const uint8_t phb_value = (uint8_t)gpio_get_level(LM_CTRL_KNOB_B);

  process_knob_channel(pha_value, &s_encoder_a_level, &s_debounce_a_cnt, +1);
  process_knob_channel(phb_value, &s_encoder_b_level, &s_debounce_b_cnt, -1);
}

static esp_err_t init_knob_gpio(gpio_num_t gpio_num) {
  const gpio_config_t gpio_cfg = {
    .pin_bit_mask = (1ULL << gpio_num),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&gpio_cfg);
}

esp_err_t lm_ctrl_input_init(QueueHandle_t event_queue) {
  if (event_queue == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_knob_timer != NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  s_event_queue = event_queue;
  s_count_value = 0;
  s_debounce_a_cnt = 0;
  s_debounce_b_cnt = 0;

  ESP_RETURN_ON_ERROR(init_knob_gpio(LM_CTRL_KNOB_A), TAG, "Encoder A gpio init failed");
  ESP_RETURN_ON_ERROR(init_knob_gpio(LM_CTRL_KNOB_B), TAG, "Encoder B gpio init failed");

  s_encoder_a_level = (uint8_t)gpio_get_level(LM_CTRL_KNOB_A);
  s_encoder_b_level = (uint8_t)gpio_get_level(LM_CTRL_KNOB_B);

  const esp_timer_create_args_t knob_timer_args = {
    .callback = knob_poll_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lm_knob",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&knob_timer_args, &s_knob_timer), TAG, "Failed to create knob timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_knob_timer, LM_CTRL_KNOB_POLL_INTERVAL_US), TAG, "Failed to start knob timer");

  ESP_LOGI(TAG, "Vendor-style ring input initialized on A=%d B=%d", LM_CTRL_KNOB_A, LM_CTRL_KNOB_B);

  /* Button polling via PCF8574 (always active) */
  const esp_timer_create_args_t btn_timer_args = {
    .callback = btn_poll_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lm_btn",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&btn_timer_args, &s_btn_timer), TAG, "Failed to create button timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_btn_timer, LM_CTRL_BTN_POLL_INTERVAL_US), TAG, "Failed to start button timer");

  ESP_LOGI(TAG, "PCF8574 button polling started (P5, 50 ms interval)");

  const esp_timer_create_args_t touch_debug_timer_args = {
    .callback        = touch_debug_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name            = "lm_touch_dbg",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&touch_debug_timer_args, &s_touch_debug_timer), TAG, "Failed to create touch debug timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_touch_debug_timer, LM_CTRL_TOUCH_DEBUG_INTERVAL_US), TAG, "Failed to start touch debug timer");
  ESP_LOGI(TAG, "Touch debug polling started (2 s interval)");

  return ESP_OK;
}
#else
esp_err_t lm_ctrl_input_init(QueueHandle_t event_queue) {
  s_event_queue = event_queue;
  ESP_LOGW(TAG, "Encoder disabled — PCF8574 button polling only");

  const esp_timer_create_args_t btn_timer_args = {
    .callback = btn_poll_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lm_btn",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&btn_timer_args, &s_btn_timer), TAG, "Failed to create button timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_btn_timer, LM_CTRL_BTN_POLL_INTERVAL_US), TAG, "Failed to start button timer");

  ESP_LOGI(TAG, "PCF8574 button polling started (P5, 50 ms interval)");

  const esp_timer_create_args_t touch_debug_timer_args = {
    .callback        = touch_debug_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name            = "lm_touch_dbg",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&touch_debug_timer_args, &s_touch_debug_timer), TAG, "Failed to create touch debug timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_touch_debug_timer, LM_CTRL_TOUCH_DEBUG_INTERVAL_US), TAG, "Failed to start touch debug timer");
  ESP_LOGI(TAG, "Touch debug polling started (2 s interval)");

  return ESP_OK;
}
#endif
