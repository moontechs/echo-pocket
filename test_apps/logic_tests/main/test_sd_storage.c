/** @file test_sd_storage.c
 * @brief Unity tests for SD storage path generation and error handling.
 *
 * Tests the directory-path constants and error string mapping. The actual
 * FAT filesystem operations (mount, mkdir) cannot run under logic_tests
 * without a real SD card — those paths are verified via the on-device
 * manual check documented in the plan.
 */

#include <string.h>
#include "unity.h"
#include "sd_storage.h"

/* ── Path correctness ────────────────────────────────────────────────── */

void test_mount_point_not_empty(void)
{
    TEST_ASSERT_TRUE(strlen(SD_MOUNT_POINT) > 0);
}

void test_app_root_nested_under_mount(void)
{
    /* SD_APP_ROOT must start with SD_MOUNT_POINT + "/" */
    TEST_ASSERT_EQUAL_INT(0, strncmp(SD_APP_ROOT, SD_MOUNT_POINT, strlen(SD_MOUNT_POINT)));
    /* Must be a sub-path, not exactly equal */
    TEST_ASSERT_TRUE(strlen(SD_APP_ROOT) > strlen(SD_MOUNT_POINT));
}

void test_subdirs_under_app_root(void)
{
    /* Every subdirectory constant must be nested under SD_APP_ROOT */
    TEST_ASSERT_EQUAL_INT(0, strncmp(SD_CONFIG_DIR, SD_APP_ROOT, strlen(SD_APP_ROOT)));
    TEST_ASSERT_EQUAL_INT(0, strncmp(SD_REC_DIR, SD_APP_ROOT, strlen(SD_APP_ROOT)));
    TEST_ASSERT_EQUAL_INT(0, strncmp(SD_QUEUE_DIR, SD_APP_ROOT, strlen(SD_APP_ROOT)));
    TEST_ASSERT_EQUAL_INT(0, strncmp(SD_LOGS_DIR, SD_APP_ROOT, strlen(SD_APP_ROOT)));
}

void test_subdirs_are_distinct(void)
{
    TEST_ASSERT_TRUE(strcmp(SD_CONFIG_DIR, SD_REC_DIR) != 0);
    TEST_ASSERT_TRUE(strcmp(SD_CONFIG_DIR, SD_QUEUE_DIR) != 0);
    TEST_ASSERT_TRUE(strcmp(SD_CONFIG_DIR, SD_LOGS_DIR) != 0);
    TEST_ASSERT_TRUE(strcmp(SD_REC_DIR, SD_QUEUE_DIR) != 0);
    TEST_ASSERT_TRUE(strcmp(SD_REC_DIR, SD_LOGS_DIR) != 0);
    TEST_ASSERT_TRUE(strcmp(SD_QUEUE_DIR, SD_LOGS_DIR) != 0);
}

void test_subdir_names_are_expected(void)
{
    /* Verify the leaf directory names match the spec */
    const char *p;
    p = strrchr(SD_CONFIG_DIR, '/'); TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("config", p + 1);

    p = strrchr(SD_REC_DIR, '/'); TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("rec", p + 1);

    p = strrchr(SD_QUEUE_DIR, '/'); TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("queue", p + 1);

    p = strrchr(SD_LOGS_DIR, '/'); TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("logs", p + 1);
}

/* ── Error string mapping ────────────────────────────────────────────── */

void test_error_strings_are_distinct(void)
{
    const char *s_ok       = sd_storage_err_str(SD_STORAGE_OK);
    const char *s_mount    = sd_storage_err_str(SD_STORAGE_ERR_MOUNT_FAILED);
    const char *s_dir      = sd_storage_err_str(SD_STORAGE_ERR_DIR_FAILED);
    const char *s_notmount = sd_storage_err_str(SD_STORAGE_ERR_NOT_MOUNTED);

    /* Each error code maps to a non-NULL, non-empty string */
    TEST_ASSERT_NOT_NULL(s_ok);
    TEST_ASSERT_NOT_NULL(s_mount);
    TEST_ASSERT_NOT_NULL(s_dir);
    TEST_ASSERT_NOT_NULL(s_notmount);
    TEST_ASSERT_TRUE(strlen(s_ok) > 0);
    TEST_ASSERT_TRUE(strlen(s_mount) > 0);
    TEST_ASSERT_TRUE(strlen(s_dir) > 0);
    TEST_ASSERT_TRUE(strlen(s_notmount) > 0);

    /* They should all differ (no catch-all collision) */
    TEST_ASSERT_TRUE(strcmp(s_ok, s_mount) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_dir) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_notmount) != 0);
    TEST_ASSERT_TRUE(strcmp(s_mount, s_dir) != 0);
    TEST_ASSERT_TRUE(strcmp(s_mount, s_notmount) != 0);
    TEST_ASSERT_TRUE(strcmp(s_dir, s_notmount) != 0);
}
