Project Brief: ESP-IDF Hardware Porting (La Marzocco Knob Controller)

1. Project Overview

The objective is to port an existing open-source ESP-IDF project (La Marzocco Knob Controller) from its original target hardware to a new ESP32-S3 rotary display.

The core application logic, Wi-Fi, BLE, and cloud synchronization components are functioning perfectly on the new MCU. The scope of work is strictly limited to adapting the hardware Abstraction Layer (HAL): specifically the display driver, touch driver, and rotary encoder pin mapping.

No UI redesign is required. The goal is to get the existing UI rendering correctly on the new display hardware.

2. Hardware Specifications

Original Source Hardware (JC3636K718)

MCU: ESP32-S3

Display Interface: QSPI (Quad SPI)

Display IC: ST77916

Resolution: 360x360

Touch IC: CST816S (I2C)

Target Hardware (Elecrow ESP32 Display 2.1" V1.0)

MCU: ESP32-S3 (WROOM-1)

Display Interface: 16-bit RGB (DPI) + 3-Wire SPI (for initialization)

Display IC: ST7701S

Resolution: 480x480

Touch IC: CST820 (I2C)

Reference Repository: Elecrow GitHub (CrowPanel 2.1")

3. Scope of Work

Phase 1: Display Driver Migration (QSPI to RGB)

This is the most critical task. The original project uses esp_lcd_panel_io_qspi to drive the ST77916. The Elecrow board uses a 16-bit RGB interface driven by the ST7701S.

Remove QSPI implementation: In the project's display initialization code (likely lm_ctrl_display_init), strip out the QSPI bus and ST77916 initialization.

Implement RGB interface: Use the ESP-IDF esp_lcd_panel_rgb API.

3-Wire SPI Initialization: The ST7701S requires initialization commands sent over a 3-wire SPI interface (SDA, SCL, CS) before the RGB bus takes over. You can extract the exact initialization hex array from the Elecrow GitHub repository linked above (look in their example/ folder).

Pin Mapping Update: Map the ESP32-S3 GPIOs to the Elecrow RGB pins. You must extract these from the Elecrow schematic/example code.

Phase 2: UI Resolution Adaptation (360x360 to 480x480)

The client does not want to pay for a time-consuming UI redesign. Please use one of the following LVGL strategies to adapt the 360x360 UI to the 480x480 screen:

Option A (Preferred MVP): Centered with Black Bezels. Initialize the lv_disp_drv_t with hor_res = 480 and ver_res = 480. Wrap the root La Marzocco UI container in an lv_obj set to exactly 360x360 and align it to LV_ALIGN_CENTER.

Option B: LVGL Scaling. Render at 360x360 and use LVGL's display scaling/zoom transformations if performance allows.

Option C: Responsive Flow. If the original author used lv_pct() (percentages) and flexbox layouts, simply change the root resolution to 480x480 and allow LVGL to stretch the elements.

Phase 3: Touch & Input Migration

Touch Driver: The original code uses a CST816S via I2C (LM_CTRL_TOUCH_HOST I2C_NUM_0). The Elecrow board typically uses a CST820. Update the I2C read/write logic to match the CST820 registers. (Refer to the Elecrow repository for register maps).

Rotary Encoder: Update LM_CTRL_KNOB_A and LM_CTRL_KNOB_B in lm_ctrl_display_config.h to the correct Elecrow GPIO pins.

Backlight (PWM): Update LM_CTRL_LCD_BL to the correct GPIO and ensure the LEDC timer is configured correctly for the Elecrow hardware.

4. Specific File References

Based on the existing codebase, you will primarily need to modify:

lm_ctrl_display_config.h: Update all #define GPIO mappings. Remove QSPI definitions, add RGB data pin definitions (D0-D15, HSYNC, VSYNC, DE, PCLK).

display.c (or wherever lm_ctrl_display_init is implemented): Rewrite the esp_lcd initialization block.

5. Acceptance Criteria

Project compiles successfully under ESP-IDF v5.x / v6.x.

The display initializes without visual corruption or tearing.

The La Marzocco UI is visible, centered, and proportional (no distorted text).

The rotary knob scrolls through the UI.

The capacitive touch screen registers clicks accurately.
