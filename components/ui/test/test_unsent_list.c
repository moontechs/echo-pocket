/** @file test_unsent_list.c
 * @brief Regression test for the Unsent list screen reading real queue
 *        entries — previously a stub that always reported 0 items
 *        regardless of what was actually queued (see AGENTS.md UI §).
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "unity.h"
#include "list_screens.h"
#include "queue_store.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Fixed scratch dir (no mkdtemp — not declared under the "linux" IDF
 * target's host toolchain). Tests don't run concurrently, so a fixed
 * path is safe; each call just re-truncates the same queue file. */
#define TEST_BASE_PATH "/tmp/echo_pocket_unsent_test"

static queue_index_t *make_test_queue(void)
{
    mkdir(TEST_BASE_PATH, 0755);
    char queue_dir[300];
    snprintf(queue_dir, sizeof(queue_dir), "%s/queue", TEST_BASE_PATH);
    mkdir(queue_dir, 0755);

    char index_path[350];
    snprintf(index_path, sizeof(index_path), "%s/index.json", queue_dir);
    remove(index_path); /* start each test from an empty queue */

    queue_store_err_t err;
    queue_index_t *queue = queue_store_init(TEST_BASE_PATH, &err);
    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(QUEUE_STORE_OK, err);
    return queue;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

void test_unsent_list_reads_pending_and_failed(void)
{
    queue_index_t *queue = make_test_queue();

    /* REC_A: enqueued then explicitly marked FAILED.
     * REC_B: enqueued then explicitly marked SENT.
     * REC_C: enqueued and left PENDING.
     * Unsent should report exactly REC_A + REC_C (2 items) — REC_B must
     * not show up. */
    TEST_ASSERT_EQUAL(QUEUE_STORE_OK,
        queue_store_enqueue(queue, "REC_A", "/rec/REC_A.wav", 1000, 2000));
    TEST_ASSERT_EQUAL(QUEUE_STORE_OK,
        queue_store_enqueue(queue, "REC_B", "/rec/REC_B.wav", 1000, 2000));
    TEST_ASSERT_EQUAL(QUEUE_STORE_OK,
        queue_store_enqueue(queue, "REC_C", "/rec/REC_C.wav", 1000, 2000));

    int count = 0;
    const queue_entry_t *entries = queue_store_get_entries(queue, &count);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL_STRING("REC_A", entries[0].id);
    TEST_ASSERT_EQUAL_STRING("REC_B", entries[1].id);
    TEST_ASSERT_EQUAL_STRING("REC_C", entries[2].id);

    /* queue_store_get_entries() returns a const view; the mutating API
     * takes non-const queue_entry_t* pointers into the same array. */
    queue_store_mark_failed(queue, (queue_entry_t *)&entries[0]);
    queue_store_mark_sent(queue, (queue_entry_t *)&entries[1], 42);

    unsent_list_enter(queue);
    TEST_ASSERT_EQUAL(2, unsent_list_get_item_count());

    queue_store_deinit(queue);
}

void test_unsent_list_empty_queue(void)
{
    queue_index_t *queue = make_test_queue();

    unsent_list_enter(queue);
    TEST_ASSERT_EQUAL(0, unsent_list_get_item_count());

    queue_store_deinit(queue);
}

void test_unsent_list_null_queue_safe(void)
{
    unsent_list_enter(NULL);
    TEST_ASSERT_EQUAL(0, unsent_list_get_item_count());
}
