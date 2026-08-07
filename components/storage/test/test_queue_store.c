/** @file test_queue_store.c
 * @brief Unity tests for queue store pure-logic functions.
 *
 * Tests queue_serialize, queue_deserialize, queue_recover_uploading,
 * queue_state_str/from_str — all pure functions with no filesystem
 * or ESP-IDF dependencies.
 *
 * File I/O (queue_store_init/enqueue/flush) is verified on-device
 * per the plan.
 */

#include <string.h>
#include "unity.h"
#include "queue_store.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void fill_entry(queue_entry_t *e, const char *id, const char *file,
                       queue_state_t state, uint32_t dur, uint32_t size,
                       int attempts, int msg_id)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->id, id, QUEUE_ID_MAX - 1);
    strncpy(e->file, file, QUEUE_FILE_MAX - 1);
    e->state = state;
    e->duration_ms = dur;
    e->size = size;
    e->attempts = attempts;
    e->telegram_message_id = msg_id;
}

/* ── Test: state strings ─────────────────────────────────────────────── */

void test_queue_state_strings_all(void)
{
    TEST_ASSERT_EQUAL_STRING("recording", queue_state_str(QUEUE_STATE_RECORDING));
    TEST_ASSERT_EQUAL_STRING("pending",   queue_state_str(QUEUE_STATE_PENDING));
    TEST_ASSERT_EQUAL_STRING("uploading", queue_state_str(QUEUE_STATE_UPLOADING));
    TEST_ASSERT_EQUAL_STRING("sent",      queue_state_str(QUEUE_STATE_SENT));
    TEST_ASSERT_EQUAL_STRING("failed",    queue_state_str(QUEUE_STATE_FAILED));
}

void test_queue_state_strings_invalid(void)
{
    /* Out of range uses "unknown" */
    TEST_ASSERT_EQUAL_STRING("unknown", queue_state_str(99));
}

void test_queue_state_from_str_all(void)
{
    TEST_ASSERT_EQUAL(QUEUE_STATE_RECORDING, queue_state_from_str("recording"));
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING,   queue_state_from_str("pending"));
    TEST_ASSERT_EQUAL(QUEUE_STATE_UPLOADING, queue_state_from_str("uploading"));
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT,      queue_state_from_str("sent"));
    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED,    queue_state_from_str("failed"));
}

void test_queue_state_from_str_unknown(void)
{
    /* Unknown strings fall back to PENDING */
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, queue_state_from_str("bogus"));
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, queue_state_from_str(""));
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, queue_state_from_str(NULL));
}

/* ── Test: serialize empty ───────────────────────────────────────────── */

void test_serialize_empty_queue(void)
{
    char buf[256];
    size_t len = queue_serialize(NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    /* Should be just "[\n]\n" */
    const char *expected = "[\n]\n";
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_EQUAL(strlen(expected), len);
}

void test_serialize_null_params(void)
{
    queue_entry_t entries[1];
    fill_entry(&entries[0], "test", "file.wav", QUEUE_STATE_PENDING, 1000, 1024, 0, 0);

    /* NULL entries */
    TEST_ASSERT_EQUAL(0, queue_serialize(NULL, 1, NULL, 0));
    /* NULL buf */
    char buf[64];
    TEST_ASSERT_EQUAL(0, queue_serialize(entries, 1, NULL, 64));
    /* zero buf_size */
    TEST_ASSERT_EQUAL(0, queue_serialize(entries, 1, buf, 0));
    /* negative count */
    TEST_ASSERT_EQUAL(0, queue_serialize(entries, -1, buf, sizeof(buf)));
}

/* ── Test: serialize single entry ────────────────────────────────────── */

void test_serialize_single_pending(void)
{
    queue_entry_t entries[1];
    fill_entry(&entries[0], "REC_001", "/sdcard/echo-pocket/rec/REC_001.wav",
               QUEUE_STATE_PENDING, 60000, 1920000, 0, 0);

    char buf[1024];
    size_t len = queue_serialize(entries, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < sizeof(buf));

    /* Verify key fields appear in the output */
    TEST_ASSERT_TRUE(strstr(buf, "\"id\": \"REC_001\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"file\": \"/sdcard/echo-pocket/rec/REC_001.wav\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"state\": \"pending\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"duration_ms\": 60000") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"size\": 1920000") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"attempts\": 0") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"telegram_message_id\": 0") != NULL);
}

void test_serialize_single_sent(void)
{
    queue_entry_t entries[1];
    fill_entry(&entries[0], "REC_SENT", "/sdcard/echo-pocket/rec/REC_SENT.wav",
               QUEUE_STATE_SENT, 120000, 3840000, 1, 42);

    char buf[1024];
    size_t len = queue_serialize(entries, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_TRUE(strstr(buf, "\"state\": \"sent\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"telegram_message_id\": 42") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"attempts\": 1") != NULL);
}

void test_serialize_single_failed(void)
{
    queue_entry_t entries[1];
    fill_entry(&entries[0], "REC_FAIL", "/sd/rec/REC_FAIL.wav",
               QUEUE_STATE_FAILED, 5000, 160000, 5, 0);

    char buf[1024];
    size_t len = queue_serialize(entries, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_TRUE(strstr(buf, "\"state\": \"failed\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"attempts\": 5") != NULL);
}

/* ── Test: serialize multiple entries ────────────────────────────────── */

void test_serialize_multiple_entries(void)
{
    queue_entry_t entries[3];
    fill_entry(&entries[0], "REC_001", "/sd/rec/REC_001.wav",
               QUEUE_STATE_PENDING, 10000, 320000, 0, 0);
    fill_entry(&entries[1], "REC_002", "/sd/rec/REC_002.wav",
               QUEUE_STATE_UPLOADING, 20000, 640000, 2, 0);
    fill_entry(&entries[2], "REC_003", "/sd/rec/REC_003.wav",
               QUEUE_STATE_SENT, 30000, 960000, 1, 99);

    char buf[2048];
    size_t len = queue_serialize(entries, 3, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < sizeof(buf));

    /* All three records present */
    TEST_ASSERT_TRUE(strstr(buf, "REC_001") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "REC_002") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "REC_003") != NULL);

    /* First two entries have trailing comma, last doesn't */
    TEST_ASSERT_TRUE(strstr(buf, "REC_001") < strstr(buf, "REC_002"));
    TEST_ASSERT_TRUE(strstr(buf, "REC_002") < strstr(buf, "REC_003"));
}

void test_serialize_buffer_too_small(void)
{
    queue_entry_t entries[3];
    for (int i = 0; i < 3; i++) {
        fill_entry(&entries[i], "REC_001", "/sd/rec/REC_001.wav",
                   QUEUE_STATE_PENDING, 10000, 320000, 0, 0);
    }

    /* Buffer too small for 3 entries */
    char tiny[32];
    size_t len = queue_serialize(entries, 3, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

/* ── Test: deserialize empty ─────────────────────────────────────────── */

void test_deserialize_empty_array(void)
{
    queue_entry_t entries[QUEUE_MAX_ENTRIES];
    int count = -1;

    int parsed = queue_deserialize("[]", entries, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(0, parsed);
    TEST_ASSERT_EQUAL(0, count);
}

void test_deserialize_null_params(void)
{
    int count = -1;

    /* NULL content */
    TEST_ASSERT_EQUAL(-1, queue_deserialize(NULL, NULL, 0, &count));
    /* NULL entries */
    TEST_ASSERT_EQUAL(-1, queue_deserialize("[]", NULL, 5, &count));
}

void test_deserialize_bad_input(void)
{
    queue_entry_t entries[QUEUE_MAX_ENTRIES];
    int count = -1;

    /* Not an array */
    TEST_ASSERT_EQUAL(-1, queue_deserialize("{}", entries, QUEUE_MAX_ENTRIES, &count));
    /* Empty string */
    TEST_ASSERT_EQUAL(-1, queue_deserialize("", entries, QUEUE_MAX_ENTRIES, &count));
    /* Whitespace only */
    TEST_ASSERT_EQUAL(-1, queue_deserialize("   ", entries, QUEUE_MAX_ENTRIES, &count));
}

/* ── Test: deserialize single entry ──────────────────────────────────── */

void test_deserialize_single(void)
{
    const char *json =
        "[\n"
        "  {\n"
        "    \"id\": \"REC_20260804_215700_001\",\n"
        "    \"file\": \"/echo-pocket/rec/REC_20260804_215700_001.wav\",\n"
        "    \"state\": \"pending\",\n"
        "    \"duration_ms\": 184000,\n"
        "    \"size\": 5888044,\n"
        "    \"attempts\": 0,\n"
        "    \"telegram_message_id\": 0\n"
        "  }\n"
        "]\n";

    queue_entry_t entries[QUEUE_MAX_ENTRIES];
    int count = 0;

    int parsed = queue_deserialize(json, entries, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(1, parsed);
    TEST_ASSERT_EQUAL(1, count);

    TEST_ASSERT_EQUAL_STRING("REC_20260804_215700_001", entries[0].id);
    TEST_ASSERT_EQUAL_STRING("/echo-pocket/rec/REC_20260804_215700_001.wav", entries[0].file);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[0].state);
    TEST_ASSERT_EQUAL_UINT32(184000, entries[0].duration_ms);
    TEST_ASSERT_EQUAL_UINT32(5888044, entries[0].size);
    TEST_ASSERT_EQUAL_INT(0, entries[0].attempts);
    TEST_ASSERT_EQUAL_INT(0, entries[0].telegram_message_id);
}

void test_deserialize_single_sent(void)
{
    const char *json =
        "[\n"
        "  {\n"
        "    \"id\": \"REC_SENT_001\",\n"
        "    \"file\": \"/sd/rec/REC_SENT_001.wav\",\n"
        "    \"state\": \"sent\",\n"
        "    \"duration_ms\": 30000,\n"
        "    \"size\": 960000,\n"
        "    \"attempts\": 1,\n"
        "    \"telegram_message_id\": 42\n"
        "  }\n"
        "]\n";

    queue_entry_t entries[QUEUE_MAX_ENTRIES];
    int count = 0;

    int parsed = queue_deserialize(json, entries, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(1, parsed);
    TEST_ASSERT_EQUAL_INT(42, entries[0].telegram_message_id);
    TEST_ASSERT_EQUAL_INT(1, entries[0].attempts);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, entries[0].state);
}

/* ── Test: deserialize multiple entries ──────────────────────────────── */

void test_deserialize_multiple(void)
{
    const char *json =
        "[\n"
        "  {\n"
        "    \"id\": \"REC_001\",\n"
        "    \"file\": \"/sd/rec/REC_001.wav\",\n"
        "    \"state\": \"pending\",\n"
        "    \"duration_ms\": 10000,\n"
        "    \"size\": 320000,\n"
        "    \"attempts\": 0,\n"
        "    \"telegram_message_id\": 0\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"REC_002\",\n"
        "    \"file\": \"/sd/rec/REC_002.wav\",\n"
        "    \"state\": \"sent\",\n"
        "    \"duration_ms\": 20000,\n"
        "    \"size\": 640000,\n"
        "    \"attempts\": 1,\n"
        "    \"telegram_message_id\": 7\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"REC_003\",\n"
        "    \"file\": \"/sd/rec/REC_003.wav\",\n"
        "    \"state\": \"failed\",\n"
        "    \"duration_ms\": 5000,\n"
        "    \"size\": 160000,\n"
        "    \"attempts\": 5,\n"
        "    \"telegram_message_id\": 0\n"
        "  }\n"
        "]\n";

    queue_entry_t entries[QUEUE_MAX_ENTRIES];
    int count = 0;

    int parsed = queue_deserialize(json, entries, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(3, parsed);
    TEST_ASSERT_EQUAL(3, count);

    TEST_ASSERT_EQUAL_STRING("REC_001", entries[0].id);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[0].state);

    TEST_ASSERT_EQUAL_STRING("REC_002", entries[1].id);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, entries[1].state);
    TEST_ASSERT_EQUAL_INT(7, entries[1].telegram_message_id);

    TEST_ASSERT_EQUAL_STRING("REC_003", entries[2].id);
    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, entries[2].state);
    TEST_ASSERT_EQUAL_INT(5, entries[2].attempts);
}

/* ── Test: round-trip ────────────────────────────────────────────────── */

void test_round_trip_serialize_deserialize(void)
{
    queue_entry_t original[4];
    fill_entry(&original[0], "REC_A", "/sd/rec/REC_A.wav",
               QUEUE_STATE_PENDING, 15000, 480000, 0, 0);
    fill_entry(&original[1], "REC_B", "/sd/rec/REC_B.wav",
               QUEUE_STATE_UPLOADING, 25000, 800000, 2, 0);
    fill_entry(&original[2], "REC_C", "/sd/rec/REC_C.wav",
               QUEUE_STATE_SENT, 35000, 1120000, 1, 55);
    fill_entry(&original[3], "REC_D", "/sd/rec/REC_D.wav",
               QUEUE_STATE_FAILED, 45000, 1440000, 5, 0);

    /* Serialize */
    char buf[4096];
    size_t len = queue_serialize(original, 4, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    /* Deserialize */
    queue_entry_t parsed[QUEUE_MAX_ENTRIES];
    int count = 0;
    int ret = queue_deserialize(buf, parsed, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(4, ret);
    TEST_ASSERT_EQUAL(4, count);

    /* Compare all fields */
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_STRING(original[i].id, parsed[i].id);
        TEST_ASSERT_EQUAL_STRING(original[i].file, parsed[i].file);
        TEST_ASSERT_EQUAL(original[i].state, parsed[i].state);
        TEST_ASSERT_EQUAL_UINT32(original[i].duration_ms, parsed[i].duration_ms);
        TEST_ASSERT_EQUAL_UINT32(original[i].size, parsed[i].size);
        TEST_ASSERT_EQUAL_INT(original[i].attempts, parsed[i].attempts);
        TEST_ASSERT_EQUAL_INT(original[i].telegram_message_id, parsed[i].telegram_message_id);
    }
}

/* ── Test: recover uploading → pending ───────────────────────────────── */

void test_recover_no_uploading(void)
{
    queue_entry_t entries[3];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_PENDING, 100, 100, 0, 0);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_SENT, 200, 200, 1, 5);
    fill_entry(&entries[2], "C", "c.wav", QUEUE_STATE_FAILED, 300, 300, 5, 0);

    int recovered = queue_recover_uploading(entries, 3);
    TEST_ASSERT_EQUAL(0, recovered);

    /* States unchanged */
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[0].state);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, entries[1].state);
    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, entries[2].state);
}

void test_recover_one_uploading(void)
{
    queue_entry_t entries[3];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_PENDING, 100, 100, 0, 0);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_UPLOADING, 200, 200, 3, 0);
    fill_entry(&entries[2], "C", "c.wav", QUEUE_STATE_FAILED, 300, 300, 5, 0);

    int recovered = queue_recover_uploading(entries, 3);
    TEST_ASSERT_EQUAL(1, recovered);

    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[0].state);
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[1].state);  /* recovered */
    TEST_ASSERT_EQUAL_INT(0, entries[1].attempts);             /* reset */
    TEST_ASSERT_EQUAL(QUEUE_STATE_FAILED, entries[2].state);
}

void test_recover_multiple_uploading(void)
{
    queue_entry_t entries[5];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_UPLOADING, 100, 100, 1, 0);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_UPLOADING, 200, 200, 3, 0);
    fill_entry(&entries[2], "C", "c.wav", QUEUE_STATE_UPLOADING, 300, 300, 5, 0);
    fill_entry(&entries[3], "D", "d.wav", QUEUE_STATE_PENDING, 400, 400, 0, 0);
    fill_entry(&entries[4], "E", "e.wav", QUEUE_STATE_SENT, 500, 500, 1, 42);

    int recovered = queue_recover_uploading(entries, 5);
    TEST_ASSERT_EQUAL(3, recovered);

    /* All uploading entries should now be pending with reset attempts */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[i].state);
        TEST_ASSERT_EQUAL_INT(0, entries[i].attempts);
    }

    /* Non-uploading entries unchanged */
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[3].state);
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, entries[4].state);
    TEST_ASSERT_EQUAL_INT(42, entries[4].telegram_message_id);
}

void test_recover_null_safety(void)
{
    TEST_ASSERT_EQUAL(0, queue_recover_uploading(NULL, 0));
    TEST_ASSERT_EQUAL(0, queue_recover_uploading(NULL, 5));
}

/* ── Test: queue_remove_by_state (backs "Delete Sent") ───────────────── */

void test_remove_by_state_removes_only_matching(void)
{
    queue_entry_t entries[4];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_SENT, 100, 100, 0, 1);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_PENDING, 200, 200, 0, 0);
    fill_entry(&entries[2], "C", "c.wav", QUEUE_STATE_SENT, 300, 300, 0, 2);
    fill_entry(&entries[3], "D", "d.wav", QUEUE_STATE_FAILED, 400, 400, 5, 0);

    int count = 4;
    int removed = queue_remove_by_state(entries, &count, QUEUE_STATE_SENT);

    TEST_ASSERT_EQUAL(2, removed);
    TEST_ASSERT_EQUAL(2, count);

    /* The two surviving entries must be the non-SENT ones, in any order. */
    bool saw_pending = false, saw_failed = false;
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_NOT_EQUAL(QUEUE_STATE_SENT, entries[i].state);
        if (entries[i].state == QUEUE_STATE_PENDING) saw_pending = true;
        if (entries[i].state == QUEUE_STATE_FAILED) saw_failed = true;
    }
    TEST_ASSERT_TRUE(saw_pending);
    TEST_ASSERT_TRUE(saw_failed);
}

void test_remove_by_state_none_matching(void)
{
    queue_entry_t entries[2];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_PENDING, 100, 100, 0, 0);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_FAILED, 200, 200, 1, 0);

    int count = 2;
    int removed = queue_remove_by_state(entries, &count, QUEUE_STATE_SENT);

    TEST_ASSERT_EQUAL(0, removed);
    TEST_ASSERT_EQUAL(2, count);
}

void test_remove_by_state_all_matching(void)
{
    queue_entry_t entries[2];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_SENT, 100, 100, 0, 1);
    fill_entry(&entries[1], "B", "b.wav", QUEUE_STATE_SENT, 200, 200, 0, 2);

    int count = 2;
    int removed = queue_remove_by_state(entries, &count, QUEUE_STATE_SENT);

    TEST_ASSERT_EQUAL(2, removed);
    TEST_ASSERT_EQUAL(0, count);
}

void test_remove_by_state_null_safety(void)
{
    int count = 3;
    TEST_ASSERT_EQUAL(0, queue_remove_by_state(NULL, &count, QUEUE_STATE_SENT));

    queue_entry_t entries[1];
    fill_entry(&entries[0], "A", "a.wav", QUEUE_STATE_SENT, 100, 100, 0, 0);
    TEST_ASSERT_EQUAL(0, queue_remove_by_state(entries, NULL, QUEUE_STATE_SENT));
}

/* ── Test: deserialize capacities ────────────────────────────────────── */

void test_deserialize_exceeds_capacity(void)
{
    queue_entry_t entries[2];  /* only room for 2 */
    const char *json =
        "[\n"
        "  {\"id\":\"A\",\"file\":\"a\",\"state\":\"pending\",\"duration_ms\":1,\"size\":1,\"attempts\":0,\"telegram_message_id\":0},\n"
        "  {\"id\":\"B\",\"file\":\"b\",\"state\":\"pending\",\"duration_ms\":1,\"size\":1,\"attempts\":0,\"telegram_message_id\":0},\n"
        "  {\"id\":\"C\",\"file\":\"c\",\"state\":\"pending\",\"duration_ms\":1,\"size\":1,\"attempts\":0,\"telegram_message_id\":0}\n"
        "]\n";

    int count = 0;
    int parsed = queue_deserialize(json, entries, 2, &count);
    TEST_ASSERT_EQUAL(-1, parsed);  /* capacity exceeded */
}

/* ── Test: error strings ─────────────────────────────────────────────── */

void test_queue_store_error_strings_distinct(void)
{
    const char *s_ok    = queue_store_err_str(QUEUE_STORE_OK);
    const char *s_ni    = queue_store_err_str(QUEUE_STORE_ERR_NOT_INITIALIZED);
    const char *s_full  = queue_store_err_str(QUEUE_STORE_ERR_FULL);
    const char *s_io    = queue_store_err_str(QUEUE_STORE_ERR_IO);
    const char *s_parse = queue_store_err_str(QUEUE_STORE_ERR_PARSE);
    const char *s_nf    = queue_store_err_str(QUEUE_STORE_ERR_NOT_FOUND);

    TEST_ASSERT_NOT_NULL(s_ok);
    TEST_ASSERT_TRUE(strlen(s_ok) > 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_ni) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_full) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_io) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_parse) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_nf) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ni, s_io) != 0);
    TEST_ASSERT_TRUE(strcmp(s_parse, s_io) != 0);
}

/* ── Test: simulates interrupted write (recovery) ─────────────────────── */

void test_recover_preserves_non_uploading_fields(void)
{
    queue_entry_t entries[2];
    fill_entry(&entries[0], "REC_KEEP", "/sd/rec/keep.wav",
               QUEUE_STATE_UPLOADING, 999999, 9999999, 3, 0);
    fill_entry(&entries[1], "REC_OK", "/sd/rec/ok.wav",
               QUEUE_STATE_SENT, 123456, 6543210, 1, 77);

    int recovered = queue_recover_uploading(entries, 2);
    TEST_ASSERT_EQUAL(1, recovered);

    /* Entry 0 recovered to pending with reset attempts */
    TEST_ASSERT_EQUAL(QUEUE_STATE_PENDING, entries[0].state);
    TEST_ASSERT_EQUAL_INT(0, entries[0].attempts);
    /* But id, file, duration, size preserved */
    TEST_ASSERT_EQUAL_STRING("REC_KEEP", entries[0].id);
    TEST_ASSERT_EQUAL_STRING("/sd/rec/keep.wav", entries[0].file);
    TEST_ASSERT_EQUAL_UINT32(999999, entries[0].duration_ms);
    TEST_ASSERT_EQUAL_UINT32(9999999, entries[0].size);

    /* Entry 1 untouched */
    TEST_ASSERT_EQUAL(QUEUE_STATE_SENT, entries[1].state);
    TEST_ASSERT_EQUAL_INT(77, entries[1].telegram_message_id);
}

/* ── Test: serialize with special characters ─────────────────────────── */

void test_serialize_escape_quotes(void)
{
    queue_entry_t entries[1];
    /* Quote in a path shouldn't happen but shouldn't break output either */
    fill_entry(&entries[0], "REC_with\"quote", "/sd/rec/with\"quote.wav",
               QUEUE_STATE_PENDING, 1000, 32000, 0, 0);

    char buf[1024];
    size_t len = queue_serialize(entries, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    /* Should be valid JSON — the quote is escaped */
    /* Deserialize it back to verify it round-trips */
    queue_entry_t parsed[QUEUE_MAX_ENTRIES];
    int count = 0;
    int ret = queue_deserialize(buf, parsed, QUEUE_MAX_ENTRIES, &count);
    TEST_ASSERT_EQUAL(1, ret);
    TEST_ASSERT_EQUAL_STRING("REC_with\"quote", parsed[0].id);
    TEST_ASSERT_EQUAL_STRING("/sd/rec/with\"quote.wav", parsed[0].file);
}
