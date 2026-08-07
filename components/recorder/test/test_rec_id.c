/** @file test_rec_id.c
 * @brief Unity tests for recording ID generation (pure logic).
 *
 * Covers:
 *   - Synced timestamp: correct YYYYMMDD_HHMMSS format
 *   - Offline boot-relative: correct REC_BOOT_<uptime>_<counter> format
 *   - Counter at 0, counter at 999 (max), counter overflow
 *   - Buffer too small
 *   - Multiple generations with advancing counter produce unique IDs
 *   - Error string coverage
 */

#include "unity.h"
#include "rec_id.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Synced timestamp ────────────────────────────────────────────────── */

void test_rec_id_synced_basic(void)
{
    /* 2026-08-04 21:57:00, counter 0 */
    struct tm tm = {
        .tm_year = 2026 - 1900,
        .tm_mon  = 8 - 1,
        .tm_mday = 4,
        .tm_hour = 21,
        .tm_min  = 57,
        .tm_sec  = 0,
    };
    /* Temporarily set TZ to UTC so localtime_r gives predictable output.
     * getenv()'s return value is only valid until the next setenv() call
     * (setenv may free/realloc the underlying string), so it must be
     * copied before TZ is overwritten rather than held across the call. */
    const char *tz_ptr = getenv("TZ");
    char *old_tz = tz_ptr ? strdup(tz_ptr) : NULL;
    setenv("TZ", "UTC", 1);
    tzset();

    time_t now = mktime(&tm);

    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), true, now, 0, 0);

    if (old_tz) {
        setenv("TZ", old_tz, 1);
        free(old_tz);
    } else {
        unsetenv("TZ");
    }
    tzset();

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_NOT_NULL(strstr(buf, "REC_20260804_215700_000"));
    TEST_ASSERT_EQUAL_STRING("REC_20260804_215700_000", buf);
}

void test_rec_id_synced_counter_advanced(void)
{
    struct tm tm = {
        .tm_year = 2027 - 1900,
        .tm_mon  = 1 - 1,
        .tm_mday = 15,
        .tm_hour = 3,
        .tm_min  = 4,
        .tm_sec  = 5,
    };
    time_t now = mktime(&tm);

    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), true, now, 999, 42);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    /* Format: REC_20270115_030405_042 */
    TEST_ASSERT_NOT_NULL(strstr(buf, "REC_20270115_030405_042"));
}

void test_rec_id_synced_midnight(void)
{
    struct tm tm = {
        .tm_year = 2026 - 1900,
        .tm_mon  = 12 - 1,
        .tm_mday = 31,
        .tm_hour = 0,
        .tm_min  = 0,
        .tm_sec  = 0,
    };
    time_t now = mktime(&tm);

    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), true, now, 0, 0);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_NOT_NULL(strstr(buf, "20261231_000000_000"));
}

void test_rec_id_synced_leap_year(void)
{
    /* 2024-02-29 is a valid leap-day date */
    struct tm tm = {
        .tm_year = 2024 - 1900,
        .tm_mon  = 2 - 1,
        .tm_mday = 29,
        .tm_hour = 12,
        .tm_min  = 0,
        .tm_sec  = 0,
    };
    time_t now = mktime(&tm);

    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), true, now, 0, 0);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_NOT_NULL(strstr(buf, "20240229_120000_000"));
}

void test_rec_id_synced_max_counter(void)
{
    struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = 0, .tm_mday = 1 };
    time_t now = mktime(&tm);

    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), true, now, 999, 999);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    /* Should end with _999 */
    TEST_ASSERT_NOT_NULL(strstr(buf, "_999"));
}

/* ── Offline boot-relative ───────────────────────────────────────────── */

void test_rec_id_offline_zero_uptime(void)
{
    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 0);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_000000_000", buf);
}

void test_rec_id_offline_with_uptime(void)
{
    char buf[REC_ID_MAX_LEN];
    /* uptime = 123456 seconds (~34 hours), counter = 7 */
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 123456, 7);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_123456_007", buf);
}

void test_rec_id_offline_max_uptime(void)
{
    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0,
                                       999999, 999);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_999999_999", buf);
}

void test_rec_id_offline_uptime_wraps_at_output_size(void)
{
    /* UINT32_MAX = 4294967295 — larger than format field width (6 digits).
     * snprintf will write the full number, exceeding the field width.
     * This is fine — the format %06" PRIu32 " just minimum-width pads. */
    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0,
                                       4294967295u, 0);

    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    /* Should contain the full number, not truncated */
    TEST_ASSERT_NOT_NULL(strstr(buf, "4294967295"));
}

/* ── Counter overflow ────────────────────────────────────────────────── */

void test_rec_id_counter_overflow(void)
{
    char buf[REC_ID_MAX_LEN];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 1000);

    TEST_ASSERT_EQUAL(REC_ID_ERR_COUNTER_OVERFLOW, err);
}

void test_rec_id_counter_at_boundary(void)
{
    char buf[REC_ID_MAX_LEN];
    /* 999 is valid */
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 999);
    TEST_ASSERT_EQUAL(REC_ID_OK, err);

    /* 1000 is invalid */
    err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 1000);
    TEST_ASSERT_EQUAL(REC_ID_ERR_COUNTER_OVERFLOW, err);
}

/* ── Buffer too small ────────────────────────────────────────────────── */

void test_rec_id_buffer_too_small(void)
{
    char tiny_buf[4];
    rec_id_err_t err = rec_id_generate(tiny_buf, sizeof(tiny_buf),
                                       false, 0, 0, 0);
    TEST_ASSERT_EQUAL(REC_ID_ERR_BUFFER_TOO_SMALL, err);
}

void test_rec_id_buffer_exact_size(void)
{
    /* "REC_BOOT_000000_000" = 19 chars + NUL = 20 bytes */
    char buf[20];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 0);
    TEST_ASSERT_EQUAL(REC_ID_OK, err);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_000000_000", buf);
}

void test_rec_id_buffer_one_byte_too_small(void)
{
    /* "REC_BOOT_000000_000" needs 20 bytes total.
     * 19 bytes (including NUL) is too small. */
    char buf[19];
    rec_id_err_t err = rec_id_generate(buf, sizeof(buf), false, 0, 0, 0);
    TEST_ASSERT_EQUAL(REC_ID_ERR_BUFFER_TOO_SMALL, err);
}

void test_rec_id_null_buffer(void)
{
    rec_id_err_t err = rec_id_generate(NULL, 64, false, 0, 0, 0);
    TEST_ASSERT_EQUAL(REC_ID_ERR_BUFFER_TOO_SMALL, err);
}

void test_rec_id_zero_buf_size(void)
{
    char buf[64];
    rec_id_err_t err = rec_id_generate(buf, 0, false, 0, 0, 0);
    TEST_ASSERT_EQUAL(REC_ID_ERR_BUFFER_TOO_SMALL, err);
}

/* ── Uniqueness ──────────────────────────────────────────────────────── */

void test_rec_id_consecutive_are_unique(void)
{
    struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = 0, .tm_mday = 1 };
    time_t now = mktime(&tm);

    char prev[REC_ID_MAX_LEN] = "";
    char curr[REC_ID_MAX_LEN];

    for (uint32_t i = 0; i < 10; i++) {
        rec_id_err_t err = rec_id_generate(curr, sizeof(curr), true, now, 0, i);
        TEST_ASSERT_EQUAL(REC_ID_OK, err);
        if (i > 0) {
            TEST_ASSERT_MESSAGE(strcmp(prev, curr) != 0,
                                "Consecutive IDs must be unique");
        }
        strcpy(prev, curr);
    }
}

void test_rec_id_offline_consecutive_are_unique(void)
{
    char prev[REC_ID_MAX_LEN] = "";
    char curr[REC_ID_MAX_LEN];

    for (uint32_t i = 0; i < 10; i++) {
        rec_id_err_t err = rec_id_generate(curr, sizeof(curr), false,
                                           0, 100, i);
        TEST_ASSERT_EQUAL(REC_ID_OK, err);
        if (i > 0) {
            TEST_ASSERT_MESSAGE(strcmp(prev, curr) != 0,
                                "Consecutive offline IDs must be unique");
        }
        strcpy(prev, curr);
    }
}

/* ── Error strings ───────────────────────────────────────────────────── */

void test_rec_id_error_strings_distinct(void)
{
    const char *s_ok   = rec_id_err_str(REC_ID_OK);
    const char *s_buf  = rec_id_err_str(REC_ID_ERR_BUFFER_TOO_SMALL);
    const char *s_cnt  = rec_id_err_str(REC_ID_ERR_COUNTER_OVERFLOW);

    TEST_ASSERT_NOT_NULL(s_ok);
    TEST_ASSERT_NOT_NULL(s_buf);
    TEST_ASSERT_NOT_NULL(s_cnt);

    /* All three must be different strings */
    TEST_ASSERT_MESSAGE(strcmp(s_ok, s_buf) != 0,
                        "OK and BUFFER_TOO_SMALL strings must differ");
    TEST_ASSERT_MESSAGE(strcmp(s_ok, s_cnt) != 0,
                        "OK and COUNTER_OVERFLOW strings must differ");
    TEST_ASSERT_MESSAGE(strcmp(s_buf, s_cnt) != 0,
                        "BUFFER_TOO_SMALL and COUNTER_OVERFLOW strings must differ");
}
