# Screen Fix TODO — Based on Actual Current Code

Everything works (BLE, cloud, machine state). Screen is blank. Here is exactly what is wrong and what to fix, based on ALL files now in the chat.

---

## Status: What Is Already Confirmed Working

From `app_main.c` (now in chat):

```c
ESP_ERROR_CHECK(lm_ctrl_display_init(&display));
ESP_ERROR_CHECK(lm_ctrl_backlight_init());
ESP_ERROR_CHECK(lm_ctrl_backlight_on());
```

- ✅ `lm_ctrl_backlight_on()` IS called, immediately after display init. Backlight is not the cause of darkness.
- ✅ Init order is correct: display → backlight → wifi → machine link → UI.
- ✅ `lm_ctrl_leds_prepare_for_reset()` is NOT called from `app_main.c` directly — the GPIO_NUM_NC crash only triggers through the wifi reset path, not on normal boot.
- ✅ RGB data pin order `B0..B4, G0..G5, R0..R4` matches ESP-IDF `[D0..D15]` expectation.
- ✅ SPI D/C bit polarity is correct (`is_cmd ? 0 : 1`).
- ✅ PCF8574 power sequence order (P3→P4→P0→P2) matches factory source exactly.

---

## Problem 1 — **ROOT CAUSE** — `board_display.c`: `s_init_cmds[]` does not match the factory ST7701S type5 panel

The current `s_init_cmds[]` in `board_display.c` starts with:
```c
{0xF0, (uint8_t[]){0x28}, 1, 0},
{0xF2, (uint8_t[]){0x28}, 1, 0},
{0x73, (uint8_t[]){0xF0}, 1, 0},
```

The factory `st7701_type5_init_operations` from the Elecrow `ESP32_Display_2_1-1.ino` / Arduino_GFX_Library starts with a completely different sequence involving `{0xFF, 0x77, 0x01, 0x00, 0x00, 0x10}` bank-select commands. The current table is a generic ST7701S blob that does **not** match the specific Elecrow CrowPanel 2.1" module.

**A wrong init table = panel stays black even with correct power, SPI clocking, PCF8574 sequencing, and RGB bus.**

**Fix:** Replace `s_init_cmds[]` in `board_display.c` with the exact byte-for-byte translation of `st7701_type5_init_operations` from the Arduino_GFX_Library. The table is in `Arduino_ST7701.h` inside the Arduino_GFX_Library source — look for `st7701_type5_init_operations`. Translate each entry to the `lcd_init_cmd_t` struct format already used in the file.

The factory sequence uses `BEGIN_WRITE` / `WRITE_COMMAND_8` / `WRITE_DATA_8` / `END_WRITE` / `DELAY` macros — these map directly to the `{cmd, data[], data_bytes, delay_ms}` struct already in place.

---

## Problem 2 — `controller_ui.c`: UI is hardcoded to 360×360 inside a 480×480 framebuffer

In `lm_ctrl_ui_init()`:
```c
lv_obj_set_size(ui->screen, 360, 360);
lv_obj_set_size(ui->ring, 320, 320);
lv_obj_set_size(ui->heat_arc, 328, 328);
lv_obj_set_size(ui->setup_reset_arc, 314, 314);
```

The `base_scr` is already a full-screen black object. `ui->screen` is a child of `base_scr` aligned center. This means the 360×360 UI is correctly centered on the 480×480 panel with a 60px black border on each side — **this will look fine once the panel is actually showing anything**.

This is NOT causing the black screen. It is a cosmetic issue to address after the panel is confirmed working.

**Fix (post-panel-confirmation):** Either keep the centered 360×360 layout as-is (it looks intentional and clean), or scale up all widget sizes proportionally to use the full 480×480. Do not change this until the panel is confirmed alive.

---

## Problem 3 — `board_display.c`: `pcf8574_write_byte` has no NULL guard on `s_i2c_bus`

```c
static esp_err_t pcf8574_write_byte(uint8_t data) {
  i2c_device_config_t dev_cfg = { ... };
  i2c_master_dev_handle_t dev_handle;
  esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev_handle);
```

If `s_i2c_bus` is NULL (e.g. I2C init failed), this crashes rather than returning an error code. The function is called 7 times during power-on before `lcd_spi_init()`. A crash here means the SPI init never runs and the panel never wakes up.

**Fix:** Add at the top of `pcf8574_write_byte`:
```c
if (s_i2c_bus == NULL) return ESP_ERR_INVALID_STATE;
```

---

## Problem 4 — `board_display.c`: `esp_lcd_panel_disp_on_off` return value silently ignored

```c
esp_lcd_panel_disp_on_off(s_panel, true);
```

Should be:
```c
(void)esp_lcd_panel_disp_on_off(s_panel, true);
```

Minor / cosmetic. Will generate a compiler warning on strict builds. Not the black screen cause.

---

## Problem 5 — `board_leds.c`: `lm_ctrl_leds_prepare_for_reset` will crash when `LM_CTRL_LED_RING_GPIO` is `GPIO_NUM_NC`

```c
ESP_RETURN_ON_ERROR(gpio_reset_pin(LM_CTRL_LED_RING_GPIO), TAG, "Failed to reset LED GPIO");
```

`gpio_reset_pin(-1)` returns `ESP_ERR_INVALID_ARG` which causes `ESP_RETURN_ON_ERROR` to abort and log an error. This function is called through the wifi reset / factory reset path — not on normal boot, but it will break every clean reboot triggered by the setup portal.

**Fix:**
```c
if (LM_CTRL_LED_RING_GPIO >= 0) {
  ESP_RETURN_ON_ERROR(gpio_reset_pin(LM_CTRL_LED_RING_GPIO), TAG, "Failed to reset LED GPIO");
}
```

---

## Problem 6 — `sdkconfig.defaults`: Comment still says JC3636K718

```
# ESP-IDF 5.5.x defaults for the JC3636K718 round controller
```

Cosmetic. Update to CrowPanel 2.1".

---

## Ordered Fix Plan

| Priority | File | Fix | Impact | Status |
|---|---|---|---|---|
| 1 | `board_display.c` | Replace `s_init_cmds[]` with exact factory `st7701_type5_init_operations` bytes | **CRITICAL — root cause of black screen** | ✅ DONE |
| 2 | `board_display.c` | Add NULL guard to `pcf8574_write_byte` | **HIGH — crash if I2C init fails** | ✅ DONE |
| 3 | `board_leds.c` | Guard `gpio_reset_pin` against `GPIO_NUM_NC` | MEDIUM — crash on every portal-triggered reboot | ✅ DONE |
| 4 | `board_display.c` | Cast `esp_lcd_panel_disp_on_off` return to `(void)` | LOW — compiler warning | ✅ DONE |
| 5 | `controller_ui.c` | Evaluate whether to scale 360×360 UI up to 480×480 | LOW — cosmetic, do after panel confirmed working | ⏳ PENDING — only remaining item |
| 6 | `sdkconfig.defaults` | Update comment | cosmetic | ✅ DONE |

---

## How to Get the Correct `s_init_cmds[]` Table

The Arduino_GFX_Library is open source. The exact init sequence for this panel is in:

```
https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST7701.h
```

Search for `st7701_type5_init_operations`. The macro format maps to the struct like this:

| Arduino_GFX macro | `lcd_init_cmd_t` field |
|---|---|
| `BEGIN_WRITE` | (start of entry) |
| `WRITE_COMMAND_8(cmd)` | `.cmd = cmd` |
| `WRITE_DATA_8(d)` | one byte in `.data[]` |
| `END_WRITE` | (end of entry, `delay_ms = 0`) |
| `DELAY(ms)` | `.delay_ms = ms` on the preceding entry |

Each `BEGIN_WRITE` / `WRITE_COMMAND_8` / one-or-more `WRITE_DATA_8` / `END_WRITE` block becomes one `lcd_init_cmd_t` row.

---

## What Does NOT Need Changing

- `app_main.c` — init order and backlight call are correct ✅
- `board_backlight.c` — LEDC_TIMER_1 at 20 kHz, GPIO 6, backlight is turned on ✅
- `board_config.h` — all pins match factory source ✅
- `idf_component.yml` — `esp_lcd_st7701` and `esp_lcd_touch_cst816s` are correct ✅
- `board_display.c` RGB timing — `pclk=14MHz`, hsync/vsync porch values match factory ✅
- `board_display.c` RGB pin array order — `B0..B4, G0..G5, R0..R4` is correct for ESP-IDF ✅
- `input.c` — encoder disabled (`LM_CTRL_ENCODER_ENABLED=0`), no conflict ✅
