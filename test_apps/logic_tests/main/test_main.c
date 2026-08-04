#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "board.h"

/* ── Pin uniqueness within each function group ───────────────────────────
 *
 * Rationale (from plan Task 1):
 *   - LCD and SD sharing an SPI bus is legitimate on combined boards,
 *     so we only assert distinctness WITHIN each function group.
 *   - We do NOT assert non-zero because GPIO0 is a legitimate (if strapping) pin.
 */

static void test_button_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_LEFT_PIN, BOARD_BTN_CENTER_PIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_LEFT_PIN, BOARD_BTN_RIGHT_PIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_CENTER_PIN, BOARD_BTN_RIGHT_PIN);
}

static void test_lcd_spi_pins_distinct(void)
{
    /* MOSI, SCLK, CS, DC, RST, BL must all differ */
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_SCLK);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_CS);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_CS);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_DC, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_DC, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_RST, BOARD_LCD_PIN_BL);
}

static void test_sd_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_CMD);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D0);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D0);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D1, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D1, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D2, BOARD_SD_PIN_D3);
}

static void test_i2c_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL);
}

static void test_i2s_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_BCK);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_WS);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_WS);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_WS, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_WS, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_DIN, BOARD_I2S_PIN_DOUT);
}

static void test_battery_pins_defined(void)
{
    /* Battery pins are optional but must be valid GPIO numbers (or NC) */
    TEST_ASSERT_TRUE(BOARD_BAT_ADC_PIN >= 0 || BOARD_BAT_ADC_PIN == GPIO_NUM_NC);
    TEST_ASSERT_TRUE(BOARD_BAT_CHARGING_PIN >= 0 || BOARD_BAT_CHARGING_PIN == GPIO_NUM_NC);
    TEST_ASSERT_TRUE(BOARD_BAT_POWER_PIN >= 0 || BOARD_BAT_POWER_PIN == GPIO_NUM_NC);
}

/* ── Button debounce tests (Task 3) ─────────────────────────────────── */
/* Defined in test_buttons.c (compiled alongside this file) */
extern void test_debounce_init_state(void);
extern void test_single_press_release(void);
extern void test_press_bounce_no_false_event(void);
extern void test_release_bounce_single_event(void);
extern void test_long_press_one_event(void);
extern void test_consecutive_presses(void);
extern void test_idle_noise_no_event(void);
extern void test_independent_instances(void);

/* ── SD storage tests (Task 4) ──────────────────────────────────────── */
/* Defined in test_sd_storage.c (compiled alongside this file) */
extern void test_mount_point_not_empty(void);
extern void test_app_root_nested_under_mount(void);
extern void test_subdirs_under_app_root(void);
extern void test_subdirs_are_distinct(void);
extern void test_subdir_names_are_expected(void);
extern void test_error_strings_are_distinct(void);
extern void test_mounted_flag_initially_false(void);

/* ── Test runner ─────────────────────────────────────────────────────── */

void app_main(void)
{
    UNITY_BEGIN();

    /* Task 1: pin distinctness */
    RUN_TEST(test_button_pins_distinct);
    RUN_TEST(test_lcd_spi_pins_distinct);
    RUN_TEST(test_sd_pins_distinct);
    RUN_TEST(test_i2c_pins_distinct);
    RUN_TEST(test_i2s_pins_distinct);
    RUN_TEST(test_battery_pins_defined);

    /* Task 3: button debounce state machine */
    RUN_TEST(test_debounce_init_state);
    RUN_TEST(test_single_press_release);
    RUN_TEST(test_press_bounce_no_false_event);
    RUN_TEST(test_release_bounce_single_event);
    RUN_TEST(test_long_press_one_event);
    RUN_TEST(test_consecutive_presses);
    RUN_TEST(test_idle_noise_no_event);
    RUN_TEST(test_independent_instances);

    /* Task 4: SD storage paths and error handling */
    RUN_TEST(test_mount_point_not_empty);
    RUN_TEST(test_app_root_nested_under_mount);
    RUN_TEST(test_subdirs_under_app_root);
    RUN_TEST(test_subdirs_are_distinct);
    RUN_TEST(test_subdir_names_are_expected);
    RUN_TEST(test_error_strings_are_distinct);
    RUN_TEST(test_mounted_flag_initially_false);

    UNITY_END();
}
