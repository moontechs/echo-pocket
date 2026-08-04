/** @file test_net_selection.c
 * @brief Unity tests for the net_selection_next pure function.
 *
 * Tests the "which network to try next" decision logic without any
 * ESP-IDF or Wi-Fi mocking framework — plain function with injected
 * inputs per the plan's Testing Strategy.
 */

#include "unity.h"
#include "net_selection.h"

/* ── First-attempt tests ─────────────────────────────────────────────── */

void test_ns_first_attempt_with_networks(void)
{
    int next = -1;
    net_next_action_t action = net_selection_next(3, -1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(0, next);
}

void test_ns_first_attempt_single_network(void)
{
    int next = -1;
    net_next_action_t action = net_selection_next(1, -1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(0, next);
}

void test_ns_first_attempt_no_networks(void)
{
    int next = 999;
    net_next_action_t action = net_selection_next(0, -1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}

void test_ns_first_attempt_negative_count(void)
{
    int next = 999;
    net_next_action_t action = net_selection_next(-1, -1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}

/* ── Already connected tests ──────────────────────────────────────────── */

void test_ns_already_connected_ignores_last_result(void)
{
    int next = 999;
    /* Even if last attempt "failed", is_connected=true means stay */
    net_next_action_t action = net_selection_next(3, 1, NET_RESULT_FAILURE,
                                                   true, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_ALREADY_CONNECTED, action);
}

void test_ns_already_connected_on_first_attempt(void)
{
    int next = 999;
    net_next_action_t action = net_selection_next(3, -1, NET_RESULT_FAILURE,
                                                   true, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_ALREADY_CONNECTED, action);
}

/* ── Success then stay ────────────────────────────────────────────────── */

void test_ns_success_stays_on_same_network(void)
{
    int next = -1;
    net_next_action_t action = net_selection_next(3, 0, NET_RESULT_SUCCESS,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(0, next);
}

void test_ns_success_on_last_network(void)
{
    int next = -1;
    net_next_action_t action = net_selection_next(3, 2, NET_RESULT_SUCCESS,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(2, next);
}

/* ── Failure then advance ─────────────────────────────────────────────── */

void test_ns_failure_advances_to_next(void)
{
    int next = -1;
    net_next_action_t action = net_selection_next(3, 0, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(1, next);
}

void test_ns_failure_advances_multiple_steps(void)
{
    /* After trying 0→fail→1→fail → should advance to 2 */
    int next = -1;
    net_next_action_t action = net_selection_next(3, 1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(2, next);
}

void test_ns_failure_last_network_returns_no_more(void)
{
    int next = 999;
    net_next_action_t action = net_selection_next(3, 2, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}

void test_ns_failure_single_network_returns_no_more(void)
{
    int next = 999;
    net_next_action_t action = net_selection_next(1, 0, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}

/* ── Full walk-through ────────────────────────────────────────────────── */

void test_ns_full_walkthrough_all_fail(void)
{
    int indices[]   = {-1, 0, 1, 2};
    int expected[]  = {0,  1, 2, -1}; /* -1 means NET_NEXT_NO_MORE */
    bool connected[] = {false, false, false, false};

    for (int step = 0; step < 4; step++) {
        int next = 999;
        net_connect_result_t result = (step > 0)
            ? NET_RESULT_FAILURE : NET_RESULT_FAILURE;

        net_next_action_t action = net_selection_next(
            3, indices[step], result, connected[step], &next);

        if (expected[step] == -1) {
            TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
        } else {
            TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
            TEST_ASSERT_EQUAL(expected[step], next);
        }
    }
}

void test_ns_full_walkthrough_success_on_second(void)
{
    /* Step 0: first (index 0, fail) */
    int next = -1;
    net_next_action_t a = net_selection_next(3, -1, NET_RESULT_FAILURE,
                                              false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, a);
    TEST_ASSERT_EQUAL(0, next);

    /* Step 1: index 0 failed, advance to 1 */
    a = net_selection_next(3, 0, NET_RESULT_FAILURE, false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, a);
    TEST_ASSERT_EQUAL(1, next);

    /* Step 2: index 1 succeeded */
    a = net_selection_next(3, 1, NET_RESULT_SUCCESS, false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, a);
    TEST_ASSERT_EQUAL(1, next);
}

/* ── NULL out_next_index safety ────────────────────────────────────────── */

void test_ns_null_out_next_on_try_index(void)
{
    net_next_action_t action = net_selection_next(3, -1, NET_RESULT_FAILURE,
                                                   false, NULL);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    /* Should not crash — just don't dereference NULL */
}

void test_ns_null_out_next_on_no_more(void)
{
    net_next_action_t action = net_selection_next(0, -1, NET_RESULT_FAILURE,
                                                   false, NULL);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}

/* ── Disconnection then retry from same point ──────────────────────────── */

void test_ns_disconnect_then_retry_same_network(void)
{
    /* Was connected on index 1, then disconnected */
    int next = -1;
    net_next_action_t action = net_selection_next(3, 1, NET_RESULT_SUCCESS,
                                                   false, &next);
    /* After a disconnect, is_connected is false.
     * last_result_success says "I was on this one and it worked before."
     * We should try it again. */
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(1, next);
}

/* ── Boundary: large wifi_count ────────────────────────────────────────── */

void test_ns_large_network_count(void)
{
    int next = -1;
    /* 5 networks (CONFIG_MAX_WIFI_NETWORKS), first attempt */
    net_next_action_t action = net_selection_next(5, -1, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_TRY_INDEX, action);
    TEST_ASSERT_EQUAL(0, next);
}

void test_ns_large_network_count_all_fail(void)
{
    /* After trying the last one (index 4 of 5), all fail */
    int next = 999;
    net_next_action_t action = net_selection_next(5, 4, NET_RESULT_FAILURE,
                                                   false, &next);
    TEST_ASSERT_EQUAL(NET_NEXT_NO_MORE, action);
}
