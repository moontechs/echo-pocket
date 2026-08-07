#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Board identity ───────────────────────────────────────────────────── */

/** Waveshare ESP32-S3-LCD-1.54 (non-touch variant). */
#define BOARD_NAME "ESP32-S3-LCD-1.54"

/* ── LCD — ST7789 over SPI (SPI2_HOST) ───────────────────────────────── */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_PIN_MOSI      GPIO_NUM_39
#define BOARD_LCD_PIN_SCLK      GPIO_NUM_38
#define BOARD_LCD_PIN_MISO      GPIO_NUM_NC       /* not connected */
#define BOARD_LCD_PIN_CS        GPIO_NUM_21
#define BOARD_LCD_PIN_DC        GPIO_NUM_45
#define BOARD_LCD_PIN_RST       GPIO_NUM_40
#define BOARD_LCD_PIN_BL        GPIO_NUM_46
#define BOARD_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define BOARD_LCD_H_RES         240
#define BOARD_LCD_V_RES         240

/* ── Buttons — active-low, short-press only ──────────────────────────── */
#define BOARD_BTN_LEFT_PIN      GPIO_NUM_0
#define BOARD_BTN_CENTER_PIN    GPIO_NUM_5
#define BOARD_BTN_RIGHT_PIN     GPIO_NUM_4

/* ── SD card — SDMMC 4-bit (not SPI, so no bus sharing with LCD) ──────── */
#define BOARD_SD_PIN_CLK        GPIO_NUM_16
#define BOARD_SD_PIN_CMD        GPIO_NUM_15
#define BOARD_SD_PIN_D0         GPIO_NUM_17
#define BOARD_SD_PIN_D1         GPIO_NUM_18
#define BOARD_SD_PIN_D2         GPIO_NUM_13
#define BOARD_SD_PIN_D3         GPIO_NUM_14

/* ── I2C bus for ES7210 codec ─────────────────────────────────────────── */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_PIN_SDA       GPIO_NUM_42
#define BOARD_I2C_PIN_SCL       GPIO_NUM_41

/* ── I2S audio — I2S_NUM_0 ────────────────────────────────────────────── */
#define BOARD_I2S_PORT          I2S_NUM_0
#define BOARD_I2S_PIN_MCK       GPIO_NUM_8
#define BOARD_I2S_PIN_BCK       GPIO_NUM_9
#define BOARD_I2S_PIN_WS        GPIO_NUM_10
#define BOARD_I2S_PIN_DIN       GPIO_NUM_11
#define BOARD_I2S_PIN_DOUT      GPIO_NUM_12
#define BOARD_I2S_PIN_PA_CTRL   GPIO_NUM_7

/* ── Battery / power management ───────────────────────────────────────── */
/*
 * HARDWARE VERDICT (from vendor example bsp_power_manager.c):
 *   - VBAT is exposed via a resistor divider to GPIO 1 (ADC1_CH0).
 *   - ADC is calibrated via eFuse curve-fitting (adc_cali).
 *   - Charger status is readable on GPIO 3 (low = charging).
 *   - Battery power can be cut via GPIO 2 (set low = power off).
 *   - No pin conflicts with LCD, SD, I2S, or I2C.
 *
 * Consumer: Task 18 (battery.c).
 */
#define BOARD_BAT_ADC_PIN       GPIO_NUM_1       /* VBAT via resistor divider */
#define BOARD_BAT_CHARGING_PIN  GPIO_NUM_3       /* low = charging */
#define BOARD_BAT_POWER_PIN     GPIO_NUM_2       /* output, high = on */

/* ── PSRAM ───────────────────────────────────────────────────────────── */
/*
 * PSRAM revision confirmed: OCTAL (ESP32-S3R8), 8 MB, 80 MHz.
 * Set via CONFIG_SPIRAM_MODE_OCT=y in sdkconfig.defaults.
 */

/* ── API ─────────────────────────────────────────────────────────────── */

/** Minimal board init. Currently a stub; fills out in later tasks. */
void board_init(void);

/**
 * @brief Cut battery power (drives BOARD_BAT_POWER_PIN low) and halt.
 *
 * Does not return — the board loses power immediately. On USB power the
 * pin drop has no effect since the board is fed directly, so this also
 * spins forever as a fallback.
 */
void board_power_off(void);

#ifdef __cplusplus
}
#endif
