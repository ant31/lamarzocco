#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

/** Display geometry and pixel format for the CrowPanel 2.1" 480x480 round panel. */
#define LM_CTRL_LCD_H_RES 480
#define LM_CTRL_LCD_V_RES 480
#define LM_CTRL_LCD_BPP 16

/** ST7701S 3-wire SPI init bus */
#define LM_CTRL_LCD_SPI_CS  GPIO_NUM_16
#define LM_CTRL_LCD_SPI_SCL GPIO_NUM_2
#define LM_CTRL_LCD_SPI_SDA GPIO_NUM_1

/** ST7701S RGB parallel bus */
#define LM_CTRL_LCD_PCLK    GPIO_NUM_41
#define LM_CTRL_LCD_VSYNC   GPIO_NUM_7
#define LM_CTRL_LCD_HSYNC   GPIO_NUM_15
#define LM_CTRL_LCD_DE      GPIO_NUM_40

#define LM_CTRL_LCD_R0      GPIO_NUM_46
#define LM_CTRL_LCD_R1      GPIO_NUM_3
#define LM_CTRL_LCD_R2      GPIO_NUM_8
#define LM_CTRL_LCD_R3      GPIO_NUM_18
#define LM_CTRL_LCD_R4      GPIO_NUM_17

#define LM_CTRL_LCD_G0      GPIO_NUM_14
#define LM_CTRL_LCD_G1      GPIO_NUM_13
#define LM_CTRL_LCD_G2      GPIO_NUM_12
#define LM_CTRL_LCD_G3      GPIO_NUM_11
#define LM_CTRL_LCD_G4      GPIO_NUM_10
#define LM_CTRL_LCD_G5      GPIO_NUM_9

#define LM_CTRL_LCD_B0      GPIO_NUM_5
#define LM_CTRL_LCD_B1      GPIO_NUM_45
#define LM_CTRL_LCD_B2      GPIO_NUM_48
#define LM_CTRL_LCD_B3      GPIO_NUM_47
#define LM_CTRL_LCD_B4      GPIO_NUM_21

/** Display Backlight */
#define LM_CTRL_LCD_BL GPIO_NUM_6

/** CST820 & PCF8574 Expander I2C bus and GPIO assignments. */
#define LM_CTRL_TOUCH_HOST I2C_NUM_0
#define LM_CTRL_TOUCH_SDA GPIO_NUM_38
#define LM_CTRL_TOUCH_SCL GPIO_NUM_39
/* Reset and INT are handled by PCF8574 */
#define LM_CTRL_TOUCH_INT GPIO_NUM_NC
#define LM_CTRL_TOUCH_RST GPIO_NUM_NC

/** Physical outer ring encoder pins and direction correction. */
#define LM_CTRL_KNOB_A GPIO_NUM_42
#define LM_CTRL_KNOB_B GPIO_NUM_4
#define LM_CTRL_KNOB_DIRECTION (-1)
#define LM_CTRL_ENCODER_ENABLED 1

/** Optional local battery telemetry exposed by the controller board. */
#define LM_CTRL_BATTERY_ADC_GPIO GPIO_NUM_NC
/* JC3636K718 v1.3 routes ETA6003 STAT only to the charge LED, not to the ESP32. */
#define LM_CTRL_BATTERY_CHARGE_GPIO GPIO_NUM_NC
#define LM_CTRL_BATTERY_CHARGE_ACTIVE_LEVEL 0
#define LM_CTRL_BATTERY_SAMPLE_INTERVAL_MS 30000UL
#define LM_CTRL_BATTERY_VOLTAGE_DIVIDER 3.0f
#define LM_CTRL_BATTERY_LOW_PERCENT 20

/** WS2812 LED ring on the controller face.
 *  GPIO 48 is also used as RGB B2 on the CrowPanel 2.1" board — the ring
 *  is therefore disabled. GPIO 43 is the on-board breath LED (not a ring). */
#define LM_CTRL_LED_RING_GPIO GPIO_NUM_NC
#define LM_CTRL_LED_RING_COUNT 0
