/** @file test_battery_curve.c
 * @brief Unity tests for battery discharge-curve lookup and
 *        threshold-to-state mapping.
 *
 * Tests pure functions from battery.h:
 *   - battery_voltage_to_percent() — voltage→percent interpolation
 *   - battery_percent_to_threshold() — threshold classification
 *   - battery_should_block_upload() — upload gating logic
 *
 * Pure math/logic — no ADC, no FreeRTOS, no hardware needed.
 */

#include <string.h>
#include "unity.h"
#include "battery.h"

/* ── Voltage → percent: boundary / table points ──────────────────────── */

static void test_voltage_above_max_is_100(void)
{
    TEST_ASSERT_EQUAL(100, battery_voltage_to_percent(4300));
    TEST_ASSERT_EQUAL(100, battery_voltage_to_percent(4200));
    TEST_ASSERT_EQUAL(100, battery_voltage_to_percent(5000));
}

static void test_voltage_below_min_is_0(void)
{
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(3300));
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(3000));
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(0));
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(-1));
}

static void test_voltage_zero_is_0(void)
{
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(0));
}

static void test_voltage_negative_is_0(void)
{
    TEST_ASSERT_EQUAL(0, battery_voltage_to_percent(-500));
}

static void test_voltage_table_points_match(void)
{
    /* Every table point should return its expected percent exactly */
    TEST_ASSERT_EQUAL(100, battery_voltage_to_percent(4200));
    TEST_ASSERT_EQUAL(95,  battery_voltage_to_percent(4100));
    TEST_ASSERT_EQUAL(83,  battery_voltage_to_percent(4000));
    TEST_ASSERT_EQUAL(71,  battery_voltage_to_percent(3900));
    TEST_ASSERT_EQUAL(64,  battery_voltage_to_percent(3850));
    TEST_ASSERT_EQUAL(56,  battery_voltage_to_percent(3800));
    TEST_ASSERT_EQUAL(46,  battery_voltage_to_percent(3750));
    TEST_ASSERT_EQUAL(36,  battery_voltage_to_percent(3700));
    TEST_ASSERT_EQUAL(26,  battery_voltage_to_percent(3650));
    TEST_ASSERT_EQUAL(19,  battery_voltage_to_percent(3600));
    TEST_ASSERT_EQUAL(13,  battery_voltage_to_percent(3550));
    TEST_ASSERT_EQUAL(8,   battery_voltage_to_percent(3500));
    TEST_ASSERT_EQUAL(5,   battery_voltage_to_percent(3450));
    TEST_ASSERT_EQUAL(3,   battery_voltage_to_percent(3400));
    TEST_ASSERT_EQUAL(1,   battery_voltage_to_percent(3350));
    TEST_ASSERT_EQUAL(0,   battery_voltage_to_percent(3300));
}

/* ── Voltage → percent: interpolation midpoints ──────────────────────── */

static void test_voltage_interpolation_mid(void)
{
    /* Midpoint between 4200(100) and 4100(95) → 97 or 98 (integer round) */
    int p = battery_voltage_to_percent(4150);
    TEST_ASSERT_TRUE(p >= 97 && p <= 98);
}

static void test_voltage_interpolation_just_above_low_end(void)
{
    /* Between 3350(1) and 3300(0), 3325 should round to 0 or 1 */
    int p = battery_voltage_to_percent(3325);
    TEST_ASSERT_TRUE(p >= 0 && p <= 1);
}

static void test_voltage_interpolation_just_below_high_end(void)
{
    /* Between 4200(100) and 4100(95), 4150 → 97 or 98 */
    int p = battery_voltage_to_percent(4150);
    TEST_ASSERT_TRUE(p >= 97 && p <= 98);
}

static void test_voltage_monotonic_descending(void)
{
    /* Higher voltage must produce ≥ percent */
    int prev = battery_voltage_to_percent(4200);
    for (int mv = 4199; mv >= 3300; mv -= 10) {
        int cur = battery_voltage_to_percent(mv);
        TEST_ASSERT_TRUE_MESSAGE(cur <= prev,
                                 "battery_voltage_to_percent must be "
                                 "monotonically non-increasing");
        prev = cur;
    }
}

static void test_voltage_le_100(void)
{
    /* All valid voltages must produce ≤ 100 */
    for (int mv = 0; mv < 4500; mv += 163) {
        int p = battery_voltage_to_percent(mv);
        TEST_ASSERT_TRUE_MESSAGE(p >= 0,
                                 "percent must be >= 0");
        TEST_ASSERT_TRUE_MESSAGE(p <= 100,
                                 "percent must be <= 100");
    }
}

/* ── Threshold classification ────────────────────────────────────────── */

static void test_percent_to_threshold_normal(void)
{
    TEST_ASSERT_EQUAL(BATTERY_NORMAL, battery_percent_to_threshold(100));
    TEST_ASSERT_EQUAL(BATTERY_NORMAL, battery_percent_to_threshold(50));
    TEST_ASSERT_EQUAL(BATTERY_NORMAL, battery_percent_to_threshold(21));
}

static void test_percent_to_threshold_warning(void)
{
    TEST_ASSERT_EQUAL(BATTERY_WARNING, battery_percent_to_threshold(20));
    TEST_ASSERT_EQUAL(BATTERY_WARNING, battery_percent_to_threshold(15));
    TEST_ASSERT_EQUAL(BATTERY_WARNING, battery_percent_to_threshold(11));
}

static void test_percent_to_threshold_critical(void)
{
    TEST_ASSERT_EQUAL(BATTERY_CRITICAL, battery_percent_to_threshold(10));
    TEST_ASSERT_EQUAL(BATTERY_CRITICAL, battery_percent_to_threshold(5));
    TEST_ASSERT_EQUAL(BATTERY_CRITICAL, battery_percent_to_threshold(1));
    TEST_ASSERT_EQUAL(BATTERY_CRITICAL, battery_percent_to_threshold(0));
}

static void test_percent_to_threshold_unknown(void)
{
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_percent_to_threshold(BATTERY_PERCENT_UNKNOWN));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_percent_to_threshold(-2));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_percent_to_threshold(-100));
}

/* ── Upload gating ───────────────────────────────────────────────────── */

static void test_block_upload_critical_large_file(void)
{
    /* 10% battery, 6MB file → blocked */
    TEST_ASSERT_TRUE(battery_should_block_upload(10, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
    TEST_ASSERT_TRUE(battery_should_block_upload(5, 100 * 1024 * 1024));
    TEST_ASSERT_TRUE(battery_should_block_upload(0, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
}

static void test_block_upload_critical_small_file(void)
{
    /* 10% battery, 1MB file → still allowed (small, won't drain battery) */
    TEST_ASSERT_FALSE(battery_should_block_upload(10, LOW_BATTERY_UPLOAD_MAX_BYTES));
    TEST_ASSERT_FALSE(battery_should_block_upload(10, 1));
    TEST_ASSERT_FALSE(battery_should_block_upload(5, LOW_BATTERY_UPLOAD_MAX_BYTES));
    TEST_ASSERT_FALSE(battery_should_block_upload(0, LOW_BATTERY_UPLOAD_MAX_BYTES));
}

static void test_block_upload_warning_allowed(void)
{
    /* 11-20%: all sizes allowed */
    TEST_ASSERT_FALSE(battery_should_block_upload(20, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
    TEST_ASSERT_FALSE(battery_should_block_upload(11, 50 * 1024 * 1024));
}

static void test_block_upload_normal_allowed(void)
{
    /* >20%: all sizes allowed */
    TEST_ASSERT_FALSE(battery_should_block_upload(21, 50 * 1024 * 1024));
    TEST_ASSERT_FALSE(battery_should_block_upload(100, 100 * 1024 * 1024));
}

static void test_block_upload_unknown_allowed(void)
{
    /* Unknown battery status → never block */
    TEST_ASSERT_FALSE(battery_should_block_upload(BATTERY_PERCENT_UNKNOWN, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
    TEST_ASSERT_FALSE(battery_should_block_upload(BATTERY_PERCENT_UNKNOWN, 50 * 1024 * 1024));
}

static void test_block_upload_critical_exact_boundary(void)
{
    /* At exactly LOW_BATTERY_UPLOAD_MAX_BYTES → still allowed (threshold is >) */
    TEST_ASSERT_FALSE(battery_should_block_upload(10, LOW_BATTERY_UPLOAD_MAX_BYTES));
}

/* ── Constant sanity checks ──────────────────────────────────────────── */

static void test_unknown_constant_is_negative(void)
{
    TEST_ASSERT_TRUE(BATTERY_PERCENT_UNKNOWN < 0);
}

static void test_low_upload_max_is_reasonable(void)
{
    /* Verify LOW_BATTERY_UPLOAD_MAX_BYTES is between 1MB and 10MB */
    TEST_ASSERT_TRUE(LOW_BATTERY_UPLOAD_MAX_BYTES >= (1 * 1024 * 1024));
    TEST_ASSERT_TRUE(LOW_BATTERY_UPLOAD_MAX_BYTES <= (10 * 1024 * 1024));
}

/* ── Full walkthrough: voltage → percent → threshold → upload decision ─ */

static void test_full_pipeline_high_battery(void)
{
    int pct = battery_voltage_to_percent(4000);
    TEST_ASSERT_TRUE(pct > 20);
    TEST_ASSERT_EQUAL(BATTERY_NORMAL, battery_percent_to_threshold(pct));
    TEST_ASSERT_FALSE(battery_should_block_upload(pct, 50 * 1024 * 1024));
}

static void test_full_pipeline_warning(void)
{
    int pct = battery_voltage_to_percent(3600);
    TEST_ASSERT_TRUE(pct > 10 && pct <= 20);
    TEST_ASSERT_EQUAL(BATTERY_WARNING, battery_percent_to_threshold(pct));
    TEST_ASSERT_FALSE(battery_should_block_upload(pct, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
}

static void test_full_pipeline_critical(void)
{
    int pct = battery_voltage_to_percent(3450);
    TEST_ASSERT_TRUE(pct <= 10);
    TEST_ASSERT_EQUAL(BATTERY_CRITICAL, battery_percent_to_threshold(pct));
    TEST_ASSERT_TRUE(battery_should_block_upload(pct, LOW_BATTERY_UPLOAD_MAX_BYTES + 1));
    TEST_ASSERT_FALSE(battery_should_block_upload(pct, LOW_BATTERY_UPLOAD_MAX_BYTES));
}
