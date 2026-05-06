#include "board_display.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

#include <string.h>

#include "board_config.h"

static const char *TAG = "lm_display";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static lv_disp_t *s_display = NULL;

typedef struct {
  uint8_t cmd;
  const uint8_t *data;
  uint8_t data_bytes;
  uint8_t delay_ms;
} lcd_init_cmd_t;

/* ST7701S type5 init sequence — translated verbatim from st7701_type5_init_operations
 * in Arduino_GFX_Library (moononournation/Arduino_GFX), which is the factory-proven
 * sequence for the Elecrow CrowPanel 2.1" 480x480 round module. */
static const lcd_init_cmd_t s_init_cmds[] = {
  /* CMD2 BK0 — page 0 */
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
  {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
  {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0}, /* VBP */
  {0xC2, (uint8_t[]){0x00, 0x02}, 2, 0},
  {0xCC, (uint8_t[]){0x10}, 1, 0},
  {0xCD, (uint8_t[]){0x08}, 1, 0},
  /* Positive voltage gamma control */
  {0xB0, (uint8_t[]){0x02, 0x13, 0x1B, 0x0D,
                      0x10, 0x05, 0x08, 0x07,
                      0x07, 0x24, 0x04, 0x11,
                      0x0E, 0x2C, 0x33, 0x1D}, 16, 0},
  /* Negative voltage gamma control */
  {0xB1, (uint8_t[]){0x05, 0x13, 0x1B, 0x0D,
                      0x11, 0x05, 0x08, 0x07,
                      0x07, 0x24, 0x04, 0x11,
                      0x0E, 0x2C, 0x33, 0x1D}, 16, 0},
  /* CMD2 BK1 — page 1 */
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
  {0xB0, (uint8_t[]){0x5D}, 1, 0},
  {0xB1, (uint8_t[]){0x43}, 1, 0}, /* VCOM amplitude */
  {0xB2, (uint8_t[]){0x81}, 1, 0}, /* VGH 12V */
  {0xB3, (uint8_t[]){0x80}, 1, 0},
  {0xB5, (uint8_t[]){0x43}, 1, 0}, /* VGL -8.3V */
  {0xB7, (uint8_t[]){0x85}, 1, 0},
  {0xB8, (uint8_t[]){0x20}, 1, 0},
  {0xC1, (uint8_t[]){0x78}, 1, 0},
  {0xC2, (uint8_t[]){0x78}, 1, 0},
  {0xD0, (uint8_t[]){0x88}, 1, 0},
  {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
  {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00,
                      0x04, 0xA0, 0x00, 0x00,
                      0x00, 0x20, 0x20}, 11, 0},
  {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00,
                      0x00}, 13, 0},
  {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
  {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
  {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0,
                      0x07, 0xEE, 0xA0, 0xA0,
                      0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00}, 16, 0},
  {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
  {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
  {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0,
                      0x08, 0xEF, 0xA0, 0xA0,
                      0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00}, 16, 0},
  {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40,
                      0x00, 0x00, 0x00}, 7, 0},
  {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA,
                      0x0A, 0xBF, 0x45, 0xFF,
                      0xFF, 0x54, 0xFB, 0xA0,
                      0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
  {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08,
                      0x3F, 0x1F}, 6, 0},
  /* CMD2 BK3 — analog power control */
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
  {0xEF, (uint8_t[]){0x08}, 1, 0},
  /* CMD2 disable — return to CMD1 */
  {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
  {0x36, (uint8_t[]){0x00}, 1, 0},
  {0x3A, (uint8_t[]){0x60}, 1, 0}, /* 0x70 RGB888, 0x60 RGB666, 0x50 RGB565 */
  /* Sleep out — wait 100 ms for oscillator */
  {0x11, NULL, 0, 100},
  /* Display on — wait 50 ms */
  {0x29, NULL, 0, 50},
};

#include "esp_rom_sys.h"

/* Read raw bytes from the CST820 and log them — called once during init to
 * confirm the chip is alive and responding with the expected chip-ID.
 * CST820 chip-ID at reg 0xAA = 0xB7; CST816S = 0xB4. */
static void touch_probe_chip_id(void) {
  if (s_i2c_bus == NULL) {
    ESP_LOGW(TAG, "touch_probe: I2C bus not ready");
    return;
  }
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = 0x15,
    .scl_speed_hz    = 100000,
  };
  i2c_master_dev_handle_t dev;
  if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev) != ESP_OK) {
    ESP_LOGE(TAG, "touch_probe: failed to add I2C device 0x15");
    return;
  }

  /* Dump registers 0x00–0x0F */
  for (uint8_t reg = 0x00; reg <= 0x0F; reg++) {
    uint8_t val = 0xFF;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &val, 1, pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "touch_probe: reg 0x%02X = 0x%02X%s",
             reg, val, err != ESP_OK ? " (ERR)" : "");
  }

  /* Chip-ID is at 0xAA */
  uint8_t reg_aa = 0xAA;
  uint8_t chip_id = 0xFF;
  esp_err_t err = i2c_master_transmit_receive(dev, &reg_aa, 1, &chip_id, 1, pdMS_TO_TICKS(20));
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "touch_probe: chip-ID (reg 0xAA) = 0x%02X (%s)",
             chip_id,
             chip_id == 0xB7 ? "CST820 — OK" :
             chip_id == 0xB4 ? "CST816S" :
             chip_id == 0xB5 ? "CST816T" :
             chip_id == 0xB6 ? "CST816D" :
             chip_id == 0x11 ? "CST826"  : "UNKNOWN");
  } else {
    ESP_LOGE(TAG, "touch_probe: chip-ID read failed: %s", esp_err_to_name(err));
  }

  i2c_master_bus_rm_device(dev);
}

/* Coordinate post-processor — log every arriving touch point and apply a
 * small Y correction to compensate for the panel's physical bezel offset. */
static void touch_offset_coordinates(esp_lcd_touch_handle_t tp,
                                     uint16_t *x, uint16_t *y,
                                     uint16_t *strength, uint8_t *point_num,
                                     uint8_t max_point_num) {
  (void)tp; (void)strength; (void)max_point_num;
  if (*point_num > 0) {
    ESP_LOGI(TAG, "touch event: %u point(s)", *point_num);
  }
  for (uint8_t i = 0; i < *point_num; i++) {
    /* Log at INFO so raw vs adjusted is always visible without verbose mode */
    ESP_LOGI(TAG, "touch raw[%u]: x=%u y=%u", i, x[i], y[i]);
    y[i] = (y[i] >= 20u) ? (y[i] - 20u) : 0u;
    ESP_LOGI(TAG, "touch adj[%u]: x=%u y=%u", i, x[i], y[i]);
  }
}

static void lcd_spi_bitbang_write_byte(uint8_t data, int is_cmd) {
  // 9-bit SPI: 1 D/C bit, 8 Data bits. SCL must idle LOW and latch on RISING edge.
  gpio_set_level(LM_CTRL_LCD_SPI_SCL, 0);
  gpio_set_level(LM_CTRL_LCD_SPI_SDA, is_cmd ? 0 : 1);
  esp_rom_delay_us(1);
  gpio_set_level(LM_CTRL_LCD_SPI_SCL, 1);
  esp_rom_delay_us(1);
  gpio_set_level(LM_CTRL_LCD_SPI_SCL, 0); // Return clock low
  
  for (int i = 7; i >= 0; i--) {
    gpio_set_level(LM_CTRL_LCD_SPI_SDA, (data >> i) & 0x01);
    esp_rom_delay_us(1);
    gpio_set_level(LM_CTRL_LCD_SPI_SCL, 1);
    esp_rom_delay_us(1);
    gpio_set_level(LM_CTRL_LCD_SPI_SCL, 0); // Return clock low
  }
}

static void lcd_spi_init(void) {
  gpio_reset_pin(LM_CTRL_LCD_SPI_CS);
  gpio_reset_pin(LM_CTRL_LCD_SPI_SCL);
  gpio_reset_pin(LM_CTRL_LCD_SPI_SDA);
  gpio_set_direction(LM_CTRL_LCD_SPI_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction(LM_CTRL_LCD_SPI_SCL, GPIO_MODE_OUTPUT);
  gpio_set_direction(LM_CTRL_LCD_SPI_SDA, GPIO_MODE_OUTPUT);
  gpio_set_level(LM_CTRL_LCD_SPI_CS, 1);
  gpio_set_level(LM_CTRL_LCD_SPI_SCL, 0); // Ensure clock starts low

  for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
    gpio_set_level(LM_CTRL_LCD_SPI_CS, 0);
    esp_rom_delay_us(1);

    lcd_spi_bitbang_write_byte(s_init_cmds[i].cmd, 1);
    for (int j = 0; j < s_init_cmds[i].data_bytes; j++) {
      lcd_spi_bitbang_write_byte(s_init_cmds[i].data[j], 0);
    }
    
    gpio_set_level(LM_CTRL_LCD_SPI_CS, 1);
    esp_rom_delay_us(1);

    if (s_init_cmds[i].delay_ms) {
      vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
    }
  }

  // DO NOT release the pins! If CS floats low, electrical noise from 
  // the RGB bus will corrupt the ST7701S registers and turn the screen off.
  gpio_set_level(LM_CTRL_LCD_SPI_CS, 1);
  gpio_set_level(LM_CTRL_LCD_SPI_SCL, 0);
  gpio_set_level(LM_CTRL_LCD_SPI_SDA, 0);
}

static esp_err_t pcf8574_write_byte(uint8_t data) {
  if (s_i2c_bus == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x21,
    .scl_speed_hz = 100000,
  };
  i2c_master_dev_handle_t dev_handle;
  esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev_handle);
  if (err != ESP_OK) return err;

  err = i2c_master_transmit(dev_handle, &data, 1, pdMS_TO_TICKS(100));
  i2c_master_bus_rm_device(dev_handle);
  return err;
}

esp_err_t lm_ctrl_display_init(lv_disp_t **out_display) {
  ESP_LOGI(TAG, "Initialize I2C bus for Expander and Touch");
  i2c_master_bus_config_t i2c_config = {
    .i2c_port = LM_CTRL_TOUCH_HOST,
    .sda_io_num = LM_CTRL_TOUCH_SDA,
    .scl_io_num = LM_CTRL_TOUCH_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 0,
    .trans_queue_depth = 0,
    .flags = {
      .enable_internal_pullup = 1,
    },
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &s_i2c_bus), TAG, "I2C bus init failed");

  ESP_LOGI(TAG, "Initialize PCF8574 and run hardware power sequence");
  uint8_t pcf_state = 0xFF; // All high initially
  
  // Power on LCD (P3 HIGH)
  pcf_state |= (1 << 3);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Reset LCD (P4 HIGH -> LOW -> HIGH)
  pcf_state |= (1 << 4);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(100));
  
  pcf_state &= ~(1 << 4);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(120)); // ST7701S requires minimum 120ms reset pulse
  
  pcf_state |= (1 << 4);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(120)); // ST7701S requires >120ms after HW reset before SPI init

  // Reset Touch (P0 LOW then HIGH)
  pcf_state &= ~(1 << 0);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(50));
  pcf_state |= (1 << 0);
  pcf8574_write_byte(pcf_state);
  vTaskDelay(pdMS_TO_TICKS(50));

  // Touch INT (P2 HIGH)
  pcf_state |= (1 << 2);
  pcf8574_write_byte(pcf_state);

  ESP_LOGI(TAG, "Run 3-wire SPI initialization");
  lcd_spi_init();

  ESP_LOGI(TAG, "Configure ST7701S RGB panel");
  esp_lcd_rgb_panel_config_t panel_config = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = {
      .pclk_hz = 12 * 1000 * 1000,
      .h_res = LM_CTRL_LCD_H_RES,
      .v_res = LM_CTRL_LCD_V_RES,
      .hsync_back_porch = 10,
      .hsync_front_porch = 20,
      .hsync_pulse_width = 10,
      .vsync_back_porch = 10,
      .vsync_front_porch = 8,
      .vsync_pulse_width = 10,
      .flags.pclk_active_neg = 0,
    },
    .data_width = 16,
    .num_fbs = 3,
    .bounce_buffer_size_px = 10 * LM_CTRL_LCD_H_RES,
    .hsync_gpio_num = LM_CTRL_LCD_HSYNC,
    .vsync_gpio_num = LM_CTRL_LCD_VSYNC,
    .de_gpio_num = LM_CTRL_LCD_DE,
    .pclk_gpio_num = LM_CTRL_LCD_PCLK,
    .disp_gpio_num = GPIO_NUM_NC,
    .data_gpio_nums = {
      LM_CTRL_LCD_B0, LM_CTRL_LCD_B1, LM_CTRL_LCD_B2, LM_CTRL_LCD_B3, LM_CTRL_LCD_B4,
      LM_CTRL_LCD_G0, LM_CTRL_LCD_G1, LM_CTRL_LCD_G2, LM_CTRL_LCD_G3, LM_CTRL_LCD_G4, LM_CTRL_LCD_G5,
      LM_CTRL_LCD_R0, LM_CTRL_LCD_R1, LM_CTRL_LCD_R2, LM_CTRL_LCD_R3, LM_CTRL_LCD_R4,
    },
    .flags.fb_in_psram = 1,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &s_panel), TAG, "RGB panel init failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Panel init failed");

  /* Zero all three PSRAM framebuffers to black so uninitialized PSRAM
   * garbage does not bleed through transparent LVGL backgrounds. */
  void *fb[3] = {NULL, NULL, NULL};
  esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 3, &fb[0], &fb[1], &fb[2]);
  if (fb_err == ESP_OK) {
    const size_t fb_pixels = (size_t)LM_CTRL_LCD_H_RES * LM_CTRL_LCD_V_RES;
    /* Dark navy blue in RGB565: R=0 G=0 B=20 → 0x0014 */
    const uint16_t DARK_BLUE = 0x0014u;
    for (int i = 0; i < 3; i++) {
      if (fb[i] != NULL) {
        uint16_t *px = (uint16_t *)fb[i];
        for (size_t p = 0; p < fb_pixels; p++) {
          px[p] = DARK_BLUE;
        }
      }
    }
    ESP_LOGI(TAG, "Framebuffers filled dark blue (%zu px each)", fb_pixels);
  } else {
    ESP_LOGW(TAG, "Could not retrieve framebuffers for fill: %s", esp_err_to_name(fb_err));
  }

  (void)esp_lcd_panel_disp_on_off(s_panel, true); // ESP_ERR_NOT_SUPPORTED is expected when disp_gpio_num is NC

  esp_lcd_touch_config_t touch_config = {
    .x_max = LM_CTRL_LCD_H_RES,
    .y_max = LM_CTRL_LCD_V_RES,
    .rst_gpio_num = LM_CTRL_TOUCH_RST,
    .int_gpio_num = LM_CTRL_TOUCH_INT,
    .levels = {
      .reset = 0,
      .interrupt = 0,
    },
    .flags = {
      .swap_xy = 0,
      .mirror_x = 0,
      .mirror_y = 0,
    },
    .process_coordinates = touch_offset_coordinates,
  };
  esp_lcd_panel_io_handle_t touch_io = NULL;
  esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
  touch_io_config.dev_addr = 0x15; /* CST820 I2C address — same as CST816S */
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &touch_io_config, &touch_io), TAG, "Touch IO init failed");
  touch_probe_chip_id();

  ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(touch_io, &touch_config, &s_touch), TAG, "Touch controller init failed");
  ESP_LOGI(TAG, "Touch controller handle: %p", (void *)s_touch);

  esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
  ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "LVGL adapter init failed");

  esp_lv_adapter_display_config_t display_config =
    ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
      s_panel,
      NULL,
      LM_CTRL_LCD_H_RES,
      LM_CTRL_LCD_V_RES,
      ESP_LV_ADAPTER_ROTATE_0
    );
  s_display = esp_lv_adapter_register_display(&display_config);
  if (s_display == NULL) {
    ESP_LOGE(TAG, "LVGL display registration failed");
    return ESP_FAIL;
  }

  esp_lv_adapter_touch_config_t lv_touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_display, s_touch);
  esp_lv_adapter_register_touch(&lv_touch_config);

  ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "LVGL adapter start failed");

  /* Log which LVGL pointer indev was registered for the touch panel. */
  lv_indev_t *touch_indev = lv_indev_get_next(NULL);
  while (touch_indev != NULL) {
    if (lv_indev_get_type(touch_indev) == LV_INDEV_TYPE_POINTER) {
      ESP_LOGI(TAG, "LVGL touch indev registered: %p", (void *)touch_indev);
      break;
    }
    touch_indev = lv_indev_get_next(touch_indev);
  }
  if (touch_indev == NULL) {
    ESP_LOGW(TAG, "No LVGL pointer indev found after touch registration — touch events will not reach UI");
  }

  if (out_display != NULL) {
    *out_display = s_display;
  }
  return ESP_OK;
}

i2c_master_bus_handle_t lm_ctrl_display_i2c_bus(void) {
  return s_i2c_bus;
}

esp_lcd_touch_handle_t lm_ctrl_display_touch_handle(void) {
  return s_touch;
}
