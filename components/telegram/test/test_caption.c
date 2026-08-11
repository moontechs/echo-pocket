/** @file test_caption.c
 * @brief Unity tests for the telegram_format_caption pure function (Task 16).
 *
 * Tests caption string formatting per AGENTS.md §Telegram:
 *   Recorder ID: <rec_id>
 *   Duration: MM:SS
 *   Device: <device_name>
 */

#include <string.h>
#include "unity.h"
#include "telegram_client.h"

/* ── Basic formatting ─────────────────────────────────────────────────── */

void test_caption_basic(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_LESS_THAN(sizeof(buf), len);

    /* Check key parts are present */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Recorder ID: REC_20260804_215700_001"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 03:04"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Device: VoiceRecorder"));
}

void test_caption_zero_duration(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 0, "MyDevice", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 00:00"));
}

void test_caption_one_second(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 1500, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    /* 1500ms = 1 second (truncated, not rounded) */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 00:01"));
}

void test_caption_59_seconds(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 59999, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 00:59"));
}

void test_caption_one_minute(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 60000, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 01:00"));
}

void test_caption_one_hour(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 3600000, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    /* 60 min → MM:SS stays within two-digit minutes (60:00) */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 60:00"));
}

void test_caption_max_18_min(void)
{
    /* 18 minutes (the max auto-split duration) */
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 1080000, "VoiceRecorder", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 18:00"));
}

void test_caption_19_min_59_sec(void)
{
    /* Near the 20 min split threshold */
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 1199000, "VoiceRecorder", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 19:59"));
}

/* ── Edge cases ──────────────────────────────────────────────────────── */

void test_caption_null_rec_id(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        NULL, 60000, "Device", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "UNKNOWN"));
}

void test_caption_null_device_name(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 60000, NULL, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "VoiceRecorder"));
}

void test_caption_null_both(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        NULL, 60000, NULL, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "UNKNOWN"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "VoiceRecorder"));
}

void test_caption_null_buffer(void)
{
    size_t len = telegram_format_caption(
        "REC_TEST", 60000, "Dev", NULL, 100);
    TEST_ASSERT_EQUAL(0, len);
}

void test_caption_zero_buf_size(void)
{
    char buf[1] = "x";
    size_t len = telegram_format_caption(
        "REC_TEST", 60000, "Dev", buf, 0);
    TEST_ASSERT_EQUAL(0, len);
}

void test_caption_buffer_exact(void)
{
    /* Calculate exact size needed */
    char large_buf[TELEGRAM_CAPTION_MAX];
    size_t exact_len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder",
        large_buf, sizeof(large_buf));

    /* Buffer exactly large_enough (including NUL) */
    char *tight_buf = malloc(exact_len + 1);
    TEST_ASSERT_NOT_NULL(tight_buf);

    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder",
        tight_buf, exact_len + 1);
    TEST_ASSERT_EQUAL(exact_len, len);
    TEST_ASSERT_EQUAL_STRING(large_buf, tight_buf);

    free(tight_buf);
}

void test_caption_buffer_one_byte_too_small(void)
{
    char large_buf[TELEGRAM_CAPTION_MAX];
    size_t exact_len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder",
        large_buf, sizeof(large_buf));

    /* Buffer that's one byte too small */
    char *small_buf = malloc(exact_len);
    TEST_ASSERT_NOT_NULL(small_buf);

    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder",
        small_buf, exact_len);
    TEST_ASSERT_EQUAL(0, len);  /* Returns 0 on truncation */

    free(small_buf);
}

void test_caption_large_duration(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    /* Very large duration (24 hours) */
    size_t len = telegram_format_caption(
        "REC_TEST", 86400000, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    /* 1440 minutes — MM:SS overflow but the formatter handles it */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 1440:00"));
}

void test_caption_empty_device_name(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 60000, "", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Device: "));
}

void test_caption_empty_rec_id(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "", 60000, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Recorder ID: "));
}

void test_caption_no_trailing_newline(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_ID", 60000, "Dev", buf, sizeof(buf));

    /* Last character should not be newline */
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_EQUAL('\n', buf[len - 1]);
}

void test_caption_subsecond_truncation(void)
{
    char buf[TELEGRAM_CAPTION_MAX];
    /* 59999 ms = 59 seconds (truncated, should be 59 not 60) */
    size_t len = telegram_format_caption(
        "REC_TEST", 59999, "Dev", buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Duration: 00:59"));
}

void test_caption_exact_expected_format(void)
{
    /* Exact string match for the AGENTS.md example */
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_20260804_215700_001", 184000, "VoiceRecorder", buf, sizeof(buf));

    const char *expected =
        "Recorder ID: REC_20260804_215700_001\n"
        "Duration: 03:04\n"
        "Device: VoiceRecorder";

    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_EQUAL(strlen(expected), len);
}

void test_caption_error_strings(void)
{
    /* Verify all error strings are distinct and non-empty */
    const char *err_strs[] = {
        telegram_err_str(TELEGRAM_OK),
        telegram_err_str(TELEGRAM_ERR_NULL_PARAM),
        telegram_err_str(TELEGRAM_ERR_FILE_NOT_FOUND),
        telegram_err_str(TELEGRAM_ERR_FILE_TOO_LARGE),
        telegram_err_str(TELEGRAM_ERR_OOM),
        telegram_err_str(TELEGRAM_ERR_CONNECT),
        telegram_err_str(TELEGRAM_ERR_HTTP),
        telegram_err_str(TELEGRAM_ERR_API),
        telegram_err_str(TELEGRAM_ERR_PARSE),
        telegram_err_str(TELEGRAM_ERR_ABORTED),
    };

    for (int i = 0; i < (int)(sizeof(err_strs) / sizeof(err_strs[0])); i++) {
        TEST_ASSERT_NOT_NULL(err_strs[i]);
        TEST_ASSERT_GREATER_THAN(0, strlen(err_strs[i]));
        for (int j = i + 1; j < (int)(sizeof(err_strs) / sizeof(err_strs[0])); j++) {
            TEST_ASSERT_TRUE(strcmp(err_strs[i], err_strs[j]) != 0);
        }
    }
}

void test_caption_default_device_name(void)
{
    /* NULL device_name → "VoiceRecorder" default */
    char buf[TELEGRAM_CAPTION_MAX];
    size_t len = telegram_format_caption(
        "REC_TEST", 30000, NULL, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Device: VoiceRecorder"));
}

/* ── Audio upload filename tests ──────────────────────────────────────── */

void test_audio_filename_synced_saturday(void)
{
    char buf[64];
    size_t len = telegram_format_audio_filename(
        "REC_20260808_183005_001", 0, 0, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("Sat, 08.08.2026, 18-30.mp3", buf);
}

void test_audio_filename_synced_known_weekdays(void)
{
    char buf[64];

    telegram_format_audio_filename("REC_20000101_000000_001", 0, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Sat, 01.01.2000, 00-00.mp3", buf);

    telegram_format_audio_filename("REC_20240229_235900_001", 0, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Thu, 29.02.2024, 23-59.mp3", buf);

    telegram_format_audio_filename("REC_19991231_120000_001", 0, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Fri, 31.12.1999, 12-00.mp3", buf);

    telegram_format_audio_filename("REC_20260101_090000_001", 0, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Thu, 01.01.2026, 09-00.mp3", buf);
}

void test_audio_filename_offline_boot_id_falls_back_without_now(void)
{
    /* now=0 / now_uptime_s=0 means "caller doesn't know" — no reconstruction. */
    char buf[64];
    size_t len = telegram_format_audio_filename(
        "REC_BOOT_012345_001", 0, 0, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_012345_001.mp3", buf);
}

void test_audio_filename_offline_boot_id_reconstructed_after_sync(void)
{
    /* Recorded at uptime=100s. Uploaded later once time has synced, at
     * uptime=700s with wall clock 2026-08-08 18:30:05 UTC — so the actual
     * recording happened 600s earlier, at 18:20:05 UTC. */
    setenv("TZ", "UTC", 1);
    tzset();

    struct tm tm_upload = {0};
    tm_upload.tm_year = 2026 - 1900;
    tm_upload.tm_mon  = 8 - 1;
    tm_upload.tm_mday = 8;
    tm_upload.tm_hour = 18;
    tm_upload.tm_min  = 30;
    tm_upload.tm_sec  = 5;
    time_t now = timegm(&tm_upload);

    char buf[64];
    size_t len = telegram_format_audio_filename(
        "REC_BOOT_000100_001", now, 700, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("Sat, 08.08.2026, 18-20.mp3", buf);
}

void test_audio_filename_offline_boot_id_future_uptime_falls_back(void)
{
    /* Malformed/impossible case: recorded uptime > current uptime.
     * Don't reconstruct into a bogus future/negative offset. */
    char buf[64];
    size_t len = telegram_format_audio_filename(
        "REC_BOOT_000700_001", 1000, 100, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("REC_BOOT_000700_001.mp3", buf);
}

void test_audio_filename_null_rec_id_falls_back(void)
{
    char buf[64];
    size_t len = telegram_format_audio_filename(NULL, 0, 0, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("voice.mp3", buf);
}

void test_audio_filename_null_buffer(void)
{
    TEST_ASSERT_EQUAL(0, telegram_format_audio_filename("REC_TEST", 0, 0, NULL, 64));
}

void test_audio_filename_zero_buf_size(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL(0, telegram_format_audio_filename("REC_TEST", 0, 0, buf, 0));
}

void test_audio_filename_buffer_too_small_falls_back_gracefully(void)
{
    /* Buffer too small even for the raw-id fallback: must not overflow,
     * and must NUL-terminate whatever fits. */
    char buf[4];
    size_t len = telegram_format_audio_filename(
        "REC_20260808_183005_001", 0, 0, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_EQUAL('\0', buf[sizeof(buf) - 1]);
}
