# ESP32 480×480 Hardware Port — Real Action Plan

## Hardware Truths (from Factory Source)
Based on `ESP32_Display_2_1-1.ino`, the hardware uses a very specific setup:
1. **I2C Expander (PCF8574 at 0x21):** Controls LCD power, LCD reset, Touch reset, Touch INT, and the Encoder switch. If this isn't initialized first, nothing will turn on.
2. **Dedicated SPI Init Pins:** The 3-wire SPI interface uses CS=16, SCK=2, SDA=1. These do *not* overlap with the RGB data lines.
3. **RGB Interface Pins:** Highly fragmented pinout across the ESP32-S3.
4. **Touch & Backlight:** Touch sits on I2C at `0x15`. Backlight is on GPIO 6.

---

## Action Plan

### Phase 1: Update `board_config.h` with Factory Pins (DONE)
We need to replace all display, touch, and encoder pins with the proven factory layout.
*   **I2C Bus:** SDA = 38, SCL = 39
*   **Backlight:** GPIO 6
*   **SPI Init:** CS = 16, SCL = 2, SDA = 1
*   **RGB Bus:** 
    *   DE=40, VSYNC=7, HSYNC=15, PCLK=41
    *   R0..4 = 46, 3, 8, 18, 17
    *   G0..5 = 14, 13, 12, 11, 10, 9
    *   B0..4 = 5, 45, 48, 47, 21
*   **Encoder:** A=42, B=4. (Switch is read via I2C PCF8574 P5).

### Phase 2: Add PCF8574 Initialization to `board_display.c` (DONE)
Before initializing the RGB panel, we must:
1. Initialize the I2C master bus (SDA=38, SCL=39).
2. Send I2C commands to `0x21` (PCF8574) to configure pins:
   *   P0 (OUTPUT): TP Reset
   *   P2 (OUTPUT): TP INT
   *   P3 (OUTPUT): LCD Power
   *   P4 (OUTPUT): LCD Reset
   *   P5 (INPUT): Encoder Switch
3. Execute the hardware power-up sequence:
   *   Set P3 HIGH (Power on)
   *   Toggle P4 LOW then HIGH (LCD Reset)
   *   Toggle P0 LOW then HIGH (Touch Reset)
   *   Set P2 HIGH (Touch INT)

### Phase 3: Update LCD and Touch Initialization (DONE)
1.  **SPI Initialization:** Bit-bang the ST7701S init sequence over GPIOs 16, 2, and 1. 
2.  **RGB Peripheral:** Initialize `esp_lcd_new_rgb_panel` using the exact 16 data pins listed above. Set `bounce_buffer_size_px = 10 * 480` to prevent PSRAM bandwidth crashes.
3.  **Touch Peripheral:** Initialize `esp_lcd_touch_new_i2c_cst816s` on the shared I2C bus at address `0x15`. The reset and interrupt GPIOs in the struct should be set to `-1` (NC) because they are handled by the expander.

### Phase 4: Handle the Encoder Switch via I2C (DONE / OPTIONAL)
The original La Marzocco firmware relies on capacitive touch for "clicks" and gestures. The physical switch on the PCF8574 (P5) does not strictly need to be mapped to an LVGL event unless custom UI interactions are added later.
*   Pins A (45) and B (42) remain standard hardware-driven encoder inputs for scrolling.

### Phase 5: Resolve Pin Conflicts (CRITICAL) (DONE)
There is a documented pin conflict between the factory RGB display code and the isolated test snippets:
*   The factory RGB initialization uses GPIO `45` (B1) and `48` (B2).
*   The standalone LED/Encoder test snippet uses GPIO `48` (LED Ring) and `45` (Encoder A).
*   **Action:** The freelancer must trace the actual hardware traces or test the isolated RGB pins. If the LED ring is truly on `48` and Encoder A is on `45`, then the RGB bus initialization in `board_display.c` must NOT claim `45` and `48` (they should likely be `GPIO_NUM_NC` for the RGB peripheral if it's a 16-bit physical bus ignoring the lower blue bits).

### Phase 6: Update Partitions (per PDF guide) (DONE)
The factory firmware is too large for the default layout. We must apply the partition sizes detailed in the provided PDF:
*   Update `partitions.csv` (or create a custom one) so that `app0` is size `0x350000` (3473408 bytes).
*   Update `spiffs` partition size to `0x90000`.
*   Ensure `sdkconfig.defaults` points to this custom CSV file (`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`).

### Phase 7: Update Dependencies (`idf_component.yml`) (DONE)
Swap the LCD driver package so ESP-IDF downloads the RGB driver instead of the QSPI one.
*   Remove: `espressif/esp_lcd_st77916`
*   Add: `espressif/esp_lcd_st7701`

### Phase 8: UI Centering (`controller_ui.c`) (DONE)
Wrap the original 360x360 UI in a centered container with a black background so it looks perfect on the 480x480 screen.
