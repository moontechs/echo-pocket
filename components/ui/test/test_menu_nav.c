/** @file test_menu_nav.c
 * @brief Unit tests for menu navigation and face submenu state machines.
 *
 * Verifies button sequences produce expected cursor positions and
 * actions, independent of rendering or hardware.
 */

#include "unity.h"
#include "menu.h"

/* ── Menu state machine ──────────────────────────────────────────────── */

void test_menu_init_cursor(void)
{
    menu_state_t state;
    menu_state_init(&state);
    TEST_ASSERT_EQUAL(MENU_ITEM_NEW_RECORDING, state.cursor);
}

void test_menu_init_null_safe(void)
{
    menu_state_init(NULL);  /* should not crash */
}

void test_menu_label_known_items(void)
{
    TEST_ASSERT_EQUAL_STRING("New Recording", menu_item_label(MENU_ITEM_NEW_RECORDING));
    TEST_ASSERT_EQUAL_STRING("Recordings",    menu_item_label(MENU_ITEM_RECORDINGS));
    TEST_ASSERT_EQUAL_STRING("Unsent",        menu_item_label(MENU_ITEM_UNSENT));
    TEST_ASSERT_EQUAL_STRING("Send All",      menu_item_label(MENU_ITEM_SEND_ALL));
    TEST_ASSERT_EQUAL_STRING("Face",          menu_item_label(MENU_ITEM_FACE));
    TEST_ASSERT_EQUAL_STRING("Delete Sent",   menu_item_label(MENU_ITEM_DELETE_SENT));
    TEST_ASSERT_EQUAL_STRING("Delete All",    menu_item_label(MENU_ITEM_DELETE_ALL));
    TEST_ASSERT_EQUAL_STRING("Info",          menu_item_label(MENU_ITEM_INFO));
    TEST_ASSERT_EQUAL_STRING("Shutdown",      menu_item_label(MENU_ITEM_SHUTDOWN));
}

void test_menu_label_unknown(void)
{
    TEST_ASSERT_EQUAL_STRING("?", menu_item_label((menu_item_t)99));
    TEST_ASSERT_EQUAL_STRING("?", menu_item_label((menu_item_t)-1));
}

void test_menu_right_moves_cursor(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* RIGHT from first item */
    menu_navigate(&state, BUTTON_RIGHT, &action);
    TEST_ASSERT_EQUAL(MENU_ITEM_RECORDINGS, state.cursor);
    TEST_ASSERT_EQUAL(MENU_ACTION_NONE, action);

    /* RIGHT again */
    menu_navigate(&state, BUTTON_RIGHT, &action);
    TEST_ASSERT_EQUAL(MENU_ITEM_UNSENT, state.cursor);
    TEST_ASSERT_EQUAL(MENU_ACTION_NONE, action);
}

void test_menu_right_wraps_around(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* Move to last item */
    for (int i = 0; i < MENU_ITEM_COUNT - 1; i++) {
        menu_navigate(&state, BUTTON_RIGHT, &action);
    }
    TEST_ASSERT_EQUAL(MENU_ITEM_SHUTDOWN, state.cursor);

    /* One more RIGHT wraps to first */
    menu_navigate(&state, BUTTON_RIGHT, &action);
    TEST_ASSERT_EQUAL(MENU_ITEM_NEW_RECORDING, state.cursor);
    TEST_ASSERT_EQUAL(MENU_ACTION_NONE, action);
}

void test_menu_left_returns_home(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* LEFT from menu → back to home */
    menu_navigate(&state, BUTTON_LEFT, &action);
    TEST_ASSERT_EQUAL(MENU_ACTION_BACK_TO_HOME, action);
}

void test_menu_center_new_recording(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* CENTER on "New Recording" */
    menu_navigate(&state, BUTTON_CENTER, &action);
    TEST_ASSERT_EQUAL(MENU_ACTION_START_RECORDING, action);
}

void test_menu_center_face(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* Move to Face (index 4) */
    for (int i = 0; i < 4; i++) {
        menu_navigate(&state, BUTTON_RIGHT, &action);
    }
    TEST_ASSERT_EQUAL(MENU_ITEM_FACE, state.cursor);

    /* CENTER on "Face" */
    menu_navigate(&state, BUTTON_CENTER, &action);
    TEST_ASSERT_EQUAL(MENU_ACTION_ENTER_FACE_SUBMENU, action);
}

void test_menu_center_send_all(void)
{
    menu_state_t state;
    menu_state_init(&state);
    menu_action_t action;

    /* Move to Send All (index 3) */
    for (int i = 0; i < 3; i++) {
        menu_navigate(&state, BUTTON_RIGHT, &action);
    }
    TEST_ASSERT_EQUAL(MENU_ITEM_SEND_ALL, state.cursor);

    menu_navigate(&state, BUTTON_CENTER, &action);
    TEST_ASSERT_EQUAL(MENU_ACTION_SEND_ALL, action);
}

void test_menu_center_stub_items(void)
{
    menu_state_t state;
    menu_action_t action;

    /* Test every stub item produces MENU_ACTION_SHOW_STUB */
    menu_item_t stubs[] = {
        MENU_ITEM_RECORDINGS, MENU_ITEM_UNSENT,
        MENU_ITEM_DELETE_SENT, MENU_ITEM_DELETE_ALL, MENU_ITEM_INFO,
        MENU_ITEM_SHUTDOWN
    };

    for (size_t s = 0; s < sizeof(stubs) / sizeof(stubs[0]); s++) {
        menu_state_init(&state);

        /* Move cursor to the stub item */
        for (int i = 0; i < (int)stubs[s]; i++) {
            menu_navigate(&state, BUTTON_RIGHT, &action);
        }
        TEST_ASSERT_EQUAL(stubs[s], state.cursor);

        menu_navigate(&state, BUTTON_CENTER, &action);
        TEST_ASSERT_EQUAL_MESSAGE(MENU_ACTION_SHOW_STUB, action,
                                  "Stub item should produce SHOW_STUB");
    }
}

void test_menu_navigate_null_safety(void)
{
    menu_state_t state;
    menu_state_init(&state);

    /* Should not crash with NULL out_action */
    menu_navigate(&state, BUTTON_RIGHT, NULL);
    menu_navigate(&state, BUTTON_CENTER, NULL);

    /* Should not crash with NULL state */
    menu_action_t action = (menu_action_t)99;
    menu_navigate(NULL, BUTTON_CENTER, &action);
    /* action should be untouched when state is NULL */
    TEST_ASSERT_EQUAL((menu_action_t)99, action);
}

void test_menu_item_count_is_nine(void)
{
    /* Regression check: the menu has exactly 9 items */
    TEST_ASSERT_EQUAL(9, MENU_ITEM_COUNT);
}

/* ── Face submenu state machine ──────────────────────────────────────── */

void test_face_submenu_init(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    TEST_ASSERT_EQUAL(0, state.cursor);
    TEST_ASSERT_EQUAL(4, state.theme_count);
}

void test_face_submenu_init_zero_count(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 0);
    TEST_ASSERT_EQUAL(1, state.theme_count); /* clamped to 1 */
}

void test_face_submenu_init_null(void)
{
    face_submenu_state_init(NULL, 4);  /* should not crash */
}

void test_face_submenu_right_moves_cursor(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    int theme_index;
    bool should_exit;

    face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(1, state.cursor);
    TEST_ASSERT_EQUAL(-1, theme_index);
    TEST_ASSERT_FALSE(should_exit);

    face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(2, state.cursor);
}

void test_face_submenu_right_wraps(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    int theme_index;
    bool should_exit;

    /* Move to last item */
    for (int i = 0; i < 3; i++) {
        face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    }
    TEST_ASSERT_EQUAL(3, state.cursor);

    /* Wrap to first */
    face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(0, state.cursor);
}

void test_face_submenu_left_exits(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    int theme_index = -1;
    bool should_exit = false;

    face_submenu_navigate(&state, BUTTON_LEFT, &theme_index, &should_exit);
    TEST_ASSERT_TRUE(should_exit);
    TEST_ASSERT_EQUAL(-1, theme_index);
}

void test_face_submenu_center_selects(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    int theme_index = -1;
    bool should_exit = false;

    /* Move to third item */
    face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    face_submenu_navigate(&state, BUTTON_RIGHT, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(2, state.cursor);

    /* Select it */
    face_submenu_navigate(&state, BUTTON_CENTER, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(2, theme_index);
    TEST_ASSERT_FALSE(should_exit);
}

void test_face_submenu_center_first(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);
    int theme_index = -1;
    bool should_exit = false;

    face_submenu_navigate(&state, BUTTON_CENTER, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(0, theme_index);
    TEST_ASSERT_FALSE(should_exit);
}

void test_face_submenu_navigate_null_safety(void)
{
    face_submenu_state_t state;
    face_submenu_state_init(&state, 4);

    /* NULL out params should not crash */
    face_submenu_navigate(&state, BUTTON_RIGHT, NULL, NULL);
    face_submenu_navigate(NULL, BUTTON_CENTER, NULL, NULL);

    int theme_index = -1;
    bool should_exit = true;
    face_submenu_navigate(NULL, BUTTON_CENTER, &theme_index, &should_exit);
    /* Should be untouched */
    TEST_ASSERT_EQUAL(-1, theme_index);
    TEST_ASSERT_TRUE(should_exit);
}

/* ── Integration: menu → face submenu → back → menu ──────────────────── */

void test_menu_to_face_and_back(void)
{
    menu_state_t mstate;
    menu_state_init(&mstate);
    menu_action_t maction;

    /* Navigate to Face item */
    for (int i = 0; i < 4; i++) {
        menu_navigate(&mstate, BUTTON_RIGHT, &maction);
    }
    TEST_ASSERT_EQUAL(MENU_ITEM_FACE, mstate.cursor);

    /* Select Face */
    menu_navigate(&mstate, BUTTON_CENTER, &maction);
    TEST_ASSERT_EQUAL(MENU_ACTION_ENTER_FACE_SUBMENU, maction);

    /* Now in face submenu */
    face_submenu_state_t fstate;
    face_submenu_state_init(&fstate, 4);
    int theme_index;
    bool should_exit;

    /* Move to second theme */
    face_submenu_navigate(&fstate, BUTTON_RIGHT, &theme_index, &should_exit);
    TEST_ASSERT_EQUAL(1, fstate.cursor);

    /* Go back to menu */
    face_submenu_navigate(&fstate, BUTTON_LEFT, &theme_index, &should_exit);
    TEST_ASSERT_TRUE(should_exit);

    /* Menu cursor should still be on Face (it doesn't reset) */
    TEST_ASSERT_EQUAL(MENU_ITEM_FACE, mstate.cursor);

    /* Go back to home */
    menu_navigate(&mstate, BUTTON_LEFT, &maction);
    TEST_ASSERT_EQUAL(MENU_ACTION_BACK_TO_HOME, maction);
}
