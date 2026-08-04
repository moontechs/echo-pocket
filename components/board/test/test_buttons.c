/**
 * @file test_buttons.c
 * @brief Unity tests for the button debounce state machine (pure logic).
 *
 * Tests the ButtonDebounce state machine with simulated GPIO transitions.
 * No real hardware needed — all tests run under logic_tests.
 */

#include "unity.h"
#include "buttons.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Feed N identical raw readings into the debounce state machine. */
void feed_n(ButtonDebounce *db, bool raw, int count, uint32_t now_ms)
{
    for (int i = 0; i < count; i++) {
        button_debounce_feed(db, raw, now_ms);
    }
}

/** Feed enough identical readings to achieve stability (DEBOUNCE_STABLE_COUNT = 6). */
void feed_stable(ButtonDebounce *db, bool raw, uint32_t now_ms)
{
    feed_n(db, raw, 6, now_ms);
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/** After init, state is released, no event pending. */
void test_debounce_init_state(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    TEST_ASSERT_FALSE(db.debounced_pressed);
    TEST_ASSERT_FALSE(db.event_pending);
    /* Feed one released sample — still no event */
    TEST_ASSERT_FALSE(button_debounce_feed(&db, false, 0));
}

/** A clean press-then-release cycle produces exactly one event on release. */
void test_single_press_release(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    /* Press — stabilize to pressed */
    int events = 0;
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, true, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(0, events);    /* no event on press */

    /* Release — stabilize to released */
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, false, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(1, events);    /* exactly one event on release */
}

/** Bouncing during press transition must not produce false events. */
void test_press_bounce_no_false_event(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    /* Simulate bouncing: alternating raw readings */
    bool seq[] = { true, false, true, false, true, true, true, true, true, true };
    int events = 0;
    for (int i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++) {
        if (button_debounce_feed(&db, seq[i], 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(0, events); /* no event — hasn't stabilized to released yet */

    /* Now release cleanly */
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, false, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(1, events); /* only now, on clean release */
}

/** Bouncing during release transition must not produce multiple events. */
void test_release_bounce_single_event(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    /* Press cleanly */
    feed_stable(&db, true, 0);

    /* Release with bounce */
    bool seq[] = { false, true, false, true, false, false, false, false, false, false };
    int events = 0;
    for (int i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++) {
        if (button_debounce_feed(&db, seq[i], 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(1, events); /* exactly one event despite bounce */
}

/** Holding a button for a long time and then releasing produces one event. */
void test_long_press_one_event(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    /* Press */
    feed_stable(&db, true, 0);

    /* Hold for a long time (many samples) */
    int events = 0;
    for (int i = 0; i < 100; i++) {
        if (button_debounce_feed(&db, true, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(0, events); /* no event while holding */

    /* Release */
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, false, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(1, events); /* one event on release */
}

/** Rapid consecutive presses each produce their own event. */
void test_consecutive_presses(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    int events = 0;

    /* First press-release */
    feed_stable(&db, true, 0);
    feed_stable(&db, false, 0);
    /* Count events accumulated during the release phase */
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, false, 0)) events++;  /* already released, just feeding more */
    }

    /* Second press-release */
    feed_stable(&db, true, 0);
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db, false, 0)) events++;
    }

    TEST_ASSERT_EQUAL_INT(2, events); /* two presses = two events */
}

/** Noise while idle (released) must not produce spurious events. */
void test_idle_noise_no_event(void)
{
    ButtonDebounce db;
    button_debounce_init(&db);

    /* Idle for a while, then a single glitch */
    feed_stable(&db, false, 0);

    bool seq[] = { true, false, false, false, false, false, false };
    int events = 0;
    for (int i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++) {
        if (button_debounce_feed(&db, seq[i], 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(0, events); /* single-sample glitch ignored */
}

/** Multiple independent debounce instances don't interfere. */
void test_independent_instances(void)
{
    ButtonDebounce db1, db2;
    button_debounce_init(&db1);
    button_debounce_init(&db2);

    /* Press db1 only */
    feed_stable(&db1, true, 0);
    TEST_ASSERT_TRUE(db1.debounced_pressed);
    TEST_ASSERT_FALSE(db2.debounced_pressed); /* db2 unaffected */

    /* Release db1 */
    int events = 0;
    for (int i = 0; i < 6; i++) {
        if (button_debounce_feed(&db1, false, 0)) events++;
    }
    TEST_ASSERT_EQUAL_INT(1, events);
    TEST_ASSERT_FALSE(db2.debounced_pressed); /* db2 still unaffected */
}
