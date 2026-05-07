# ESP32 480×480 Hardware Port — Task List
## Target: Elecrow CrowPanel 2.1" V1.0 (ST7701S, 480×480, CST820)
## Source: JC3636K718 (ST77916 QSPI, 360×360, CST816S)
## Reference: https://github.com/Elecrow-RD/CrowPanel-ESP32-Display-Course-File/tree/main/CrowPanel%202.1

---

## Phase 1 — `board_config.h` Pin and Resolution Update

All GPIO defines and resolution constants live here. This file is in the chat
and ready to edit.

### 1.1 Resolution
- [x] Change `LM_CTRL_LCD_H_RES` from `360` to `480`
- [x] Change `LM_CTRL_LCD_V_RES` from `360` to `480`
- [x] Keep `LM_CTRL_LCD_BPP` at `16`

### 1.2 Remove QSPI LCD pin defines
Remove these defines entirely (they are ST77916 QSPI-only):
- `LM_CTRL_LCD_HOST` (`SPI2_HOST`)
- `LM_CTRL_LCD_SCL` (GPIO 11)
- `LM_CTRL_LCD_CS` (GPIO 12)
- `LM_CTRL_LCD_D0` … `LM_CTRL_LCD_D3` (GPIOs 13–16)
- `LM_CTRL_LCD_RST` (GPIO 17)
- `LM_CTRL_LCD_TE` (GPIO 18)

### 1.3 Add ST7701S RGB panel pin defines
Extract exact GPIO numbers from the Elecrow schematic / example `ESP32_Display_CrowPanel21.h`.
Typical Elecrow 2.1" mapping (verify against schematic before flashing):

```c
/* ST7701S 3-wire SPI init bus */
#define LM_CTRL_LCD_SPI_CS      GPIO_NUM_39
#define LM_CTRL_LCD_SPI_SCL     GPIO_NUM_48
#define LM_CTRL_LCD_SPI_SDA     GPIO_NUM_47

/* ST7701S RGB parallel bus */
#define LM_CTRL_LCD_PCLK        GPIO_NUM_40
#define LM_CTRL_LCD_VSYNC       GPIO_NUM_41
#define LM_CTRL_LCD_HSYNC       GPIO_NUM_42
#define LM_CTRL_LCD_DE          GPIO_NUM_2
#define LM_CTRL_LCD_D0          GPIO_NUM_8
#define LM_CTRL_LCD_D1          GPIO_NUM_3
#define LM_CTRL_LCD_D2          GPIO_NUM_46
#define LM_CTRL_LCD_D3          GPIO_NUM_9
#define LM_CTRL_LCD_D4          GPIO_NUM_1
#define LM_CTRL_LCD_D5          GPIO_NUM_5
#define LM_CTRL_LCD_D6          GPIO_NUM_6
#define LM_CTRL_LCD_D7          GPIO_NUM_7
#define LM_CTRL_LCD_D8          GPIO_NUM_15
#define LM_CTRL_LCD_D9          GPIO_NUM_16
#define LM_CTRL_LCD_D10         GPIO_NUM_4
#define LM_CTRL_LCD_D11         GPIO_NUM_45
#define LM_CTRL_LCD_D12         GPIO_NUM_48  /* verify — may conflict with SPI SCL */
#define LM_CTRL_LCD_D13         GPIO_NUM_47  /* verify — may conflict with SPI SDA */
#define LM_CTRL_LCD_D14         GPIO_NUM_21
#define LM_CTRL_LCD_D15         GPIO_NUM_14
#define LM_CTRL_LCD_RST         GPIO_NUM_NC  /* pulled high on Elecrow 2.1" board */
#define LM_CTRL_LCD_BL          GPIO_NUM_38
```

> ⚠️ Verify every GPIO number against the Elecrow schematic before using.
> The 3-wire SPI lines (SDA, SCL) may be reused as RGB data lines D12/D13
> after init — confirm from Elecrow example code.

### 1.4 Update touch pin defines
Elecrow 2.1" CST820 typical mapping (verify against schematic):
- `LM_CTRL_TOUCH_HOST` stays `I2C_NUM_0`
- `LM_CTRL_TOUCH_SDA` → likely `GPIO_NUM_19`
- `LM_CTRL_TOUCH_SCL` → likely `GPIO_NUM_20`
- `LM_CTRL_TOUCH_INT` → likely `GPIO_NUM_38` or `GPIO_NUM_NC` (verify)
- `LM_CTRL_TOUCH_RST` → likely `GPIO_NUM_NC` (verify)

### 1.5 Update rotary encoder pin defines
- [x] Set `LM_CTRL_KNOB_A` and `LM_CTRL_KNOB_B` to Elecrow encoder GPIOs
  (Elecrow 2.1" may not have a physical encoder — confirm hardware)
- [x] No encoder present on Elecrow 2.1"; `LM_CTRL_ENCODER_ENABLED` set to `0`

### 1.6 Check LED ring GPIO conflict
- [x] `LM_CTRL_LED_RING_GPIO` set to `GPIO_NUM_NC` and `LM_CTRL_LED_RING_COUNT` set to `0` — LED ring disabled; no GPIO conflict

---

## Phase 2 — `idf_component.yml` Driver Swap

This file is in the chat and ready to edit.

### 2.1 Remove ST77916 driver
```
espressif/esp_lcd_st77916: ^2.0.2
```

### 2.2 Remove CST816S touch driver
```
espressif/esp_lcd_touch_cst816s: ^1.1.1
```

### 2.3 Add ST7701S RGB driver
```
espressif/esp_lcd_st7701: "*"
```
> Check the exact component name and version on
> https://components.espressif.com before inserting.

### 2.4 Add CST820 touch driver
`espressif/esp_lcd_touch_cst820` does not exist on the Espressif component
registry. The CST820 and CST816S are register-compatible; the existing
`espressif/esp_lcd_touch_cst816s: ^1.1.1` driver is used instead.

---

## Phase 3 — `board_display.c` Driver Rewrite

This file is in the chat and ready to edit.

### 3.1 Remove QSPI/ST77916 includes
Remove:
```c
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "driver/spi_master.h"
```
Add:
```c
#include "esp_lcd_st7701.h"           /* or equivalent RGB driver */
#include "esp_lcd_touch_cst820.h"     /* or direct I2C register impl */
#include "esp_lcd_panel_rgb.h"
```

### 3.2 Remove `s_init_cmds[]` ST77916 table
Delete the entire `static const st77916_lcd_init_cmd_t s_init_cmds[]` array.

### 3.3 Add ST7701S 3-wire SPI init sequence
Extract the init command hex array from the Elecrow GitHub example:
`CrowPanel 2.1/example/main/` — look for the `lcd_init_cmds` or similar table.
Implement a small `lcd_spi_init()` helper that bit-bangs the 3-wire SPI using
`LM_CTRL_LCD_SPI_CS`, `LM_CTRL_LCD_SPI_SCL`, `LM_CTRL_LCD_SPI_SDA` before
the RGB peripheral takes over.

### 3.4 Rewrite `lm_ctrl_display_init()` — LCD section
Replace the QSPI `spi_bus_initialize` + `esp_lcd_new_panel_io_spi` +
`esp_lcd_new_panel_st77916` block with:

```c
/* 1. Run 3-wire SPI init */
lcd_spi_init();   /* new helper from 3.3 */

/* 2. Configure RGB panel */
esp_lcd_rgb_panel_config_t panel_config = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = {
        .pclk_hz          = 16 * 1000 * 1000,   /* verify with Elecrow example */
        .h_res            = LM_CTRL_LCD_H_RES,
        .v_res            = LM_CTRL_LCD_V_RES,
        .hsync_back_porch  = 40,  /* extract from Elecrow example */
        .hsync_front_porch = 40,
        .hsync_pulse_width = 1,
        .vsync_back_porch  = 8,
        .vsync_front_porch = 8,
        .vsync_pulse_width = 1,
        .flags.pclk_active_neg = 0,
    },
    .data_width       = 16,
    .bits_per_pixel   = LM_CTRL_LCD_BPP,
    .num_fbs          = 1,
    .bounce_buffer_size_px = 0,
    .hsync_gpio_num   = LM_CTRL_LCD_HSYNC,
    .vsync_gpio_num   = LM_CTRL_LCD_VSYNC,
    .de_gpio_num      = LM_CTRL_LCD_DE,
    .pclk_gpio_num    = LM_CTRL_LCD_PCLK,
    .disp_gpio_num    = GPIO_NUM_NC,
    .data_gpio_nums   = {
        LM_CTRL_LCD_D0,  LM_CTRL_LCD_D1,  LM_CTRL_LCD_D2,  LM_CTRL_LCD_D3,
        LM_CTRL_LCD_D4,  LM_CTRL_LCD_D5,  LM_CTRL_LCD_D6,  LM_CTRL_LCD_D7,
        LM_CTRL_LCD_D8,  LM_CTRL_LCD_D9,  LM_CTRL_LCD_D10, LM_CTRL_LCD_D11,
        LM_CTRL_LCD_D12, LM_CTRL_LCD_D13, LM_CTRL_LCD_D14, LM_CTRL_LCD_D15,
    },
    .flags.fb_in_psram = 1,
};
ESP_RETURN_ON_ERROR(
    esp_lcd_new_rgb_panel(&panel_config, &s_panel),
    TAG, "RGB panel init failed"
);
ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),   TAG, "Panel reset failed");
ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),    TAG, "Panel init failed");
ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Panel enable failed");
```

> All timing values must be verified against the Elecrow ST7701S example.

### 3.5 Remove `s_panel_io` from RGB path
The RGB interface does not use `esp_lcd_panel_io_handle_t`.
Remove `s_panel_io` static variable and all references to it.
Update the LVGL adapter call to the non-`_io` variant if needed.

### 3.6 Update touch driver init
Replace `esp_lcd_touch_new_i2c_cst816s` with `esp_lcd_touch_new_i2c_cst820`
(or the equivalent CST820 constructor).
Replace `ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG()` with the CST820 equivalent.
Verify the CST820 I2C address (typically `0x15` or `0x1A`).

### 3.7 Update LVGL adapter display config call
`ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG` is QSPI-specific.
Replace with the RGB variant:
```c
esp_lv_adapter_display_config_t display_config =
    ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        s_panel,
        LM_CTRL_LCD_H_RES,
        LM_CTRL_LCD_V_RES,
        ESP_LV_ADAPTER_ROTATE_0
    );
```
> Verify the macro name in the installed `esp_lvgl_adapter` component version.

---

## Phase 4 — `CMakeLists.txt` Component Update

File is already in the chat.

- [x] `esp_lcd_st77916` was component-managed only (not in `REQUIRES`); no change needed
- [x] `esp_driver_rmt` and `esp_driver_ledc` confirmed present
- [x] No `esp_driver_spi` in `REQUIRES`; I2C handled via component manager — correct for new init code

---

## Phase 5 — `board_backlight.c` Verify

File is in the chat — no changes needed.

- [x] `LM_CTRL_LCD_BL` = `GPIO_NUM_38` confirmed correct for Elecrow 2.1" backlight
- [x] LEDC timer 1 / channel 0 confirmed — no conflict with RGB peripheral

---

## Phase 6 — `controller_ui.c` Resolution Centering (MVP)

File is in the chat.

### 6.1 Wrap root container to 360×360 centred on 480×480

In `lm_ctrl_ui_init()`, after `lv_scr_load(ui->screen)`, insert a centred
wrapper object sized `360×360`, then reparent the existing `ui->ring` (and all
child widgets) under it instead of directly under `ui->screen`:

```c
/* Centre the 360x360 controller UI on the 480x480 display */
lv_obj_t *ui_root = lv_obj_create(ui->screen);
lv_obj_remove_style_all(ui_root);
lv_obj_set_size(ui_root, 360, 360);
lv_obj_align(ui_root, LV_ALIGN_CENTER, 0, 0);
lv_obj_set_style_bg_color(ui_root, COLOR_BG, 0);
lv_obj_set_style_bg_opa(ui_root, LV_OPA_COVER, 0);
lv_obj_clear_flag(ui_root, LV_OBJ_FLAG_SCROLLABLE);
```

Then change every `lv_xxx_create(ui->screen, ...)` call (for `ui->ring`,
`ui->heat_arc`, `ui->setup_reset_arc`, `ui->page_dots[]`, status icons, and
all card panels) to use `ui_root` as parent instead of `ui->screen`.

> This confines all LVGL objects to the 360×360 safe area and renders the
> original black `COLOR_BG` as the border on the physical 480×480 panel.

---

## Phase 7 — `sdkconfig.defaults` Updates

File is already in the chat.

- [ ] Remove or update `CONFIG_ESPTOOLPY_FLASHMODE_QIO` if the Elecrow board
  uses a different flash mode (check Elecrow sdkconfig example)
- [ ] Add `CONFIG_ESP_LCD_RGB_ISR_IRAM_SAFE=y` to prevent frame tearing during
  cache operations
- [ ] Add `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and
  `CONFIG_SPIRAM_RODATA=y` if the RGB framebuffer lives in PSRAM
- [ ] Verify `CONFIG_LV_COLOR_16_SWAP` is still correct for the ST7701S byte
  order (may need to be `n` — test on real hardware)

---

## Phase 8 — Acceptance Checks

> Hardware validation required — flash and test on physical Elecrow CrowPanel 2.1" device.

- [ ] `idf.py build` completes with no errors
- [ ] Display initialises, no white screen, no corruption, no tearing
- [ ] La Marzocco UI circle is visible and centred; black borders left/right/top/bottom
- [ ] Touch gestures (swipe up/down/left/right) register correctly
- [x] Rotary encoder confirmed absent on Elecrow 2.1"; encoder input disabled
- [x] LED ring disabled (`LM_CTRL_LED_RING_COUNT=0`, `GPIO_NUM_NC`); no GPIO conflict
- [ ] Backlight turns on at startup

---

## Reference Links

- Elecrow CrowPanel 2.1" example code:
  https://github.com/Elecrow-RD/CrowPanel-ESP32-Display-Course-File
- ESP-IDF RGB panel API:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- Espressif component registry:
  https://components.espressif.com
- ST7701S datasheet: search `ST7701S datasheet Sitronix`
- CST820 register map: included in Elecrow driver example folder
