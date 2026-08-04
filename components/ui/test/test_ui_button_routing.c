/** @file test_ui_button_routing.c
 * @brief Unit tests for ui_screen_next() — the pure screen state machine.
 *
 * Verifies that ButtonEvents route to the correct action per current
 * screen state, independent of actual rendering or hardware.
 */

#include "unity.h"
#include "ui_task.h"

/* ── Home screen transitions ─────────────────────────────────────────── */

static void test_home_center_starts_recording(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_HOME, BUTTON_CENTER, &action);

    TEST_ASSERT_EQUAL(UI_SCREEN_RECORDING, next);
    TEST_ASSERT_EQUAL(UI_ACTION_START_RECORDING, action);
}

static void test_home_left_noop(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_HOME, BUTTON_LEFT, &action);

    TEST_ASSERT_EQUAL(UI_SCREEN_HOME, next);
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);
}

static void test_home_right_noop(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_HOME, BUTTON_RIGHT, &action);

    /* Right button from home is no-op in v1.0 — menu is Task 12 */
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME, next);
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);
}

/* ── Recording screen transitions ────────────────────────────────────── */

static void test_recording_center_stops(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_RECORDING, BUTTON_CENTER, &action);

    TEST_ASSERT_EQUAL(UI_SCREEN_SAVED, next);
    TEST_ASSERT_EQUAL(UI_ACTION_STOP_RECORDING, action);
}

static void test_recording_left_noop(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_RECORDING, BUTTON_LEFT, &action);

    /* Left = back, but during recording it should be ignored.
     * (We may add a cancel/back in Task 12.) */
    TEST_ASSERT_EQUAL(UI_SCREEN_RECORDING, next);
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);
}

static void test_recording_right_noop(void)
{
    ui_action_t action = UI_ACTION_NONE;
    ui_screen_t next = ui_screen_next(UI_SCREEN_RECORDING, BUTTON_RIGHT, &action);

    TEST_ASSERT_EQUAL(UI_SCREEN_RECORDING, next);
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);
}

/* ── Saved screen transitions ────────────────────────────────────────── */

static void test_saved_any_button_returns_home(void)
{
    ui_action_t action;

    /* Center dismisses Saved → Home */
    action = UI_ACTION_NONE;
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME,
                      ui_screen_next(UI_SCREEN_SAVED, BUTTON_CENTER, &action));
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);

    /* Left dismisses Saved → Home */
    action = UI_ACTION_NONE;
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME,
                      ui_screen_next(UI_SCREEN_SAVED, BUTTON_LEFT, &action));
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);

    /* Right dismisses Saved → Home */
    action = UI_ACTION_NONE;
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME,
                      ui_screen_next(UI_SCREEN_SAVED, BUTTON_RIGHT, &action));
    TEST_ASSERT_EQUAL(UI_ACTION_NONE, action);
}

/* ── Null safety ─────────────────────────────────────────────────────── */

static void test_screen_next_null_action(void)
{
    /* Should not crash — returns current screen */
    ui_screen_t next = ui_screen_next(UI_SCREEN_HOME, BUTTON_CENTER, NULL);
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME, next);
}

/* ── Full sequence: home → record → saved → home ─────────────────────── */

static void test_full_record_cycle(void)
{
    ui_action_t action;
    ui_screen_t screen = UI_SCREEN_HOME;

    /* Home + Center → Recording */
    screen = ui_screen_next(screen, BUTTON_CENTER, &action);
    TEST_ASSERT_EQUAL(UI_SCREEN_RECORDING, screen);
    TEST_ASSERT_EQUAL(UI_ACTION_START_RECORDING, action);

    /* Recording + Center → Saved */
    screen = ui_screen_next(screen, BUTTON_CENTER, &action);
    TEST_ASSERT_EQUAL(UI_SCREEN_SAVED, screen);
    TEST_ASSERT_EQUAL(UI_ACTION_STOP_RECORDING, action);

    /* Saved + any button → Home */
    screen = ui_screen_next(screen, BUTTON_LEFT, &action);
    TEST_ASSERT_EQUAL(UI_SCREEN_HOME, screen);
}

/* ── Screen names ────────────────────────────────────────────────────── */

static void test_screen_name_known(void)
{
    TEST_ASSERT_EQUAL_STRING("Home", ui_screen_name(UI_SCREEN_HOME));
    TEST_ASSERT_EQUAL_STRING("Recording", ui_screen_name(UI_SCREEN_RECORDING));
    TEST_ASSERT_EQUAL_STRING("Saved", ui_screen_name(UI_SCREEN_SAVED));
}

static void test_screen_name_unknown(void)
{
    /* Out-of-range value should not crash */
    const char *name = ui_screen_name((ui_screen_t)99);
    TEST_ASSERT_NOT_NULL(name);
}
