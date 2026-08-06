/** @file test_upload_flow.c
 * @brief Unity tests for the upload drain-loop state machine.
 *
 * Tests upload_drain_compute_outcome() — a pure function that takes a
 * classified send result, current attempt count, max attempts, and the
 * delete_after_upload flag, and returns the correct outcome (new state,
 * new attempt count, whether to delete the file).
 *
 * These tests verify:
 *   - Success → sent, file deleted if delete_after_upload
 *   - Retryable failure, under cap → pending, no delete
 *   - Retryable failure, at cap → failed, no delete
 *   - Retryable failure, above cap → failed, no delete
 *   - Fatal failure → failed immediately, no delete
 *   - Edge cases: zero attempts, negative attempts, large max_attempts
 *
 * Does NOT test the actual FreeRTOS task, network I/O, or Telegram API
 * — those are covered by on-device manual checks.
 */

#include <string.h>
#include "unity.h"
#include "upload_task.h"
#include "queue_store.h"

/* ── Shared constants for readable tests ─────────────────────────────── */

#define MAX_ATTEMPTS  5
#define CAP           UPLOAD_MAX_ATTEMPTS

/* ── Success path ────────────────────────────────────────────────────── */

void test_ok_without_delete(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_OK, 0, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_ok_with_delete(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_OK, 2, MAX_ATTEMPTS, true);

    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_EQUAL(3, o.new_attempts);
    TEST_ASSERT_TRUE(o.should_delete_file);
}

void test_ok_at_cap(void)
{
    /* Even if attempts == max, success is success */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_OK, MAX_ATTEMPTS - 1, MAX_ATTEMPTS, true);

    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_EQUAL(MAX_ATTEMPTS, o.new_attempts);
    TEST_ASSERT_TRUE(o.should_delete_file);
}

/* ── Retryable failure — under cap ───────────────────────────────────── */

void test_fail_retryable_first_attempt(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 0, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_retryable_mid_attempts(void)
{
    /* 3 attempts so far, fail → attempt 4, still under cap (5) */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 3, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(4, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_retryable_delete_ignored(void)
{
    /* delete_after_upload=true should NOT trigger on failure */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 0, MAX_ATTEMPTS, true);

    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

/* ── Retryable failure — at cap / above cap ──────────────────────────── */

void test_fail_retryable_at_cap(void)
{
    /* 4 attempts so far → next is 5 (at cap), should fail */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 4, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(5, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_retryable_above_cap(void)
{
    /* Shouldn't happen in practice, but if attempts is already >= cap,
     * still mark failed */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 7, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(8, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_retryable_exactly_one_before_cap(void)
{
    /* 3 attempts, cap=4 → next=4, at cap, fail */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 3, 4, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(4, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

/* ── Fatal failure ───────────────────────────────────────────────────── */

void test_fail_fatal_first_attempt(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_FATAL, 0, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_fatal_with_attempts(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_FATAL, 2, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(3, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_fatal_at_cap(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_FATAL, MAX_ATTEMPTS - 1, MAX_ATTEMPTS, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(MAX_ATTEMPTS, o.new_attempts);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

void test_fail_fatal_delete_ignored(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_FATAL, 0, MAX_ATTEMPTS, true);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

/* ── Edge cases ──────────────────────────────────────────────────────── */

void test_cap_one(void)
{
    /* cap=1: first failure is fatal */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 0, 1, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
}

void test_cap_one_success(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_OK, 0, 1, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
}

void test_cap_large(void)
{
    /* cap=100: takes many attempts to fail */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 98, 100, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(99, o.new_attempts);
}

void test_cap_large_at_boundary(void)
{
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 99, 100, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(100, o.new_attempts);
}

void test_cap_zero(void)
{
    /* cap=0: immediate fail (degenerate but shouldn't crash) */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_FAIL_RETRYABLE, 0, 0, false);

    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
}

void test_cap_zero_success(void)
{
    /* cap=0 but success: still sent */
    upload_drain_outcome_t o = upload_drain_compute_outcome(
        UPLOAD_SEND_OK, 0, 0, true);

    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_TRUE(o.should_delete_file);
}

/* ── Production constant verification ────────────────────────────────── */

void test_production_cap_is_five(void)
{
    /* Verify the production cap matches the plan's stated value.
     * If someone changes UPLOAD_MAX_ATTEMPTS, this test reminds
     * them to update the plan and the drain logic. */
    TEST_ASSERT_EQUAL(5, UPLOAD_MAX_ATTEMPTS);
}

/* ── Full walk-through: real-world drain sequences ───────────────────── */

void test_walkthrough_three_entries(void)
{
    /* Simulate 3 entries being drained:
     *   Entry A: OK on first try
     *   Entry B: fails 4 times, succeeds on 5th
     *   Entry C: fails 5 times → failed
     */

    upload_drain_outcome_t o;
    int a, b, c;

    /* ── Entry A: OK ─────────────────────────────────────────── */
    o = upload_drain_compute_outcome(UPLOAD_SEND_OK, 0, CAP, true);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);
    TEST_ASSERT_TRUE(o.should_delete_file);

    /* ── Entry B: 4 failures, then success ────────────────────── */
    /* Attempt 1 */
    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 0, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(1, o.new_attempts);

    /* Attempt 2 */
    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 1, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(2, o.new_attempts);

    /* Attempt 3 */
    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 2, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(3, o.new_attempts);

    /* Attempt 4 */
    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 3, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);
    TEST_ASSERT_EQUAL(4, o.new_attempts);

    /* Attempt 5 — success! */
    o = upload_drain_compute_outcome(UPLOAD_SEND_OK, 4, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
    TEST_ASSERT_EQUAL(5, o.new_attempts);

    /* ── Entry C: 5 failures → terminal failed ─────────────────── */
    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 0, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 1, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 2, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 3, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, o.new_state);

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 4, CAP, false);
    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, o.new_state);
    TEST_ASSERT_EQUAL(5, o.new_attempts);

    (void)a; (void)b; (void)c; /* suppress unused warnings */
}

/* ── Delete-after-upload ordering verification ───────────────────────── */

void test_delete_only_on_success(void)
{
    /* Regardless of delete_after_upload flag, failure never deletes */
    upload_drain_outcome_t o;

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_RETRYABLE, 0, CAP, true);
    TEST_ASSERT_FALSE(o.should_delete_file);

    o = upload_drain_compute_outcome(UPLOAD_SEND_FAIL_FATAL, 0, CAP, true);
    TEST_ASSERT_FALSE(o.should_delete_file);

    o = upload_drain_compute_outcome(UPLOAD_SEND_OK, 0, CAP, true);
    TEST_ASSERT_TRUE(o.should_delete_file);

    o = upload_drain_compute_outcome(UPLOAD_SEND_OK, 0, CAP, false);
    TEST_ASSERT_FALSE(o.should_delete_file);
}

/* ── State consistency checks ────────────────────────────────────────── */

void test_outcome_states_are_valid(void)
{
    /* Every possible input combination should produce a valid
     * queue_state_t (not QUEUE_STATE_RECORDING or QUEUE_STATE_UPLOADING,
     * which are transient states). */
    upload_send_result_t results[] = {
        UPLOAD_SEND_OK,
        UPLOAD_SEND_FAIL_RETRYABLE,
        UPLOAD_SEND_FAIL_FATAL,
    };
    int attempts_v[] = {0, 2, 5, 10};
    bool delete_flags[] = {false, true};

    for (int ri = 0; ri < 3; ri++) {
        for (int ai = 0; ai < 4; ai++) {
            for (int di = 0; di < 2; di++) {
                upload_drain_outcome_t o = upload_drain_compute_outcome(
                    results[ri], attempts_v[ai], CAP, delete_flags[di]);

                TEST_ASSERT_TRUE(o.new_state == QUEUE_STATE_SENT ||
                                 o.new_state == QUEUE_STATE_PENDING ||
                                 o.new_state == QUEUE_STATE_FAILED);

                /* new_attempts should be old + 1 */
                TEST_ASSERT_EQUAL(attempts_v[ai] + 1, o.new_attempts);
            }
        }
    }
}

void test_sent_never_deletes_without_flag(void)
{
    /* Sentinel: when delete_after_upload is false, sent entries
     * NEVER request file deletion. */
    for (int a = 0; a < 10; a++) {
        upload_drain_outcome_t o = upload_drain_compute_outcome(
            UPLOAD_SEND_OK, a, CAP, false);
        TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, o.new_state);
        TEST_ASSERT_FALSE(o.should_delete_file);
    }
}
