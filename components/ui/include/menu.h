/** @file menu.h
 * @brief Main menu and Face submenu types and pure navigation state machines.
 *
 * The navigation functions are pure logic — unit-testable without hardware,
 * display, or FreeRTOS.  Rendering functions are in menu.c.
 *
 * Menu structure (per AGENTS.md §4.5):
 *   New Recording / Recordings / Unsent / Send All / Face /
 *   Delete Sent / Delete All / Info / Shutdown
 *
 * Face submenu lists the 4 registered themes by display name.
 */

#pragma once

#include "buttons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Menu items ──────────────────────────────────────────────────────── */

typedef enum {
    MENU_ITEM_NEW_RECORDING = 0,
    MENU_ITEM_RECORDINGS,
    MENU_ITEM_UNSENT,
    MENU_ITEM_SEND_ALL,
    MENU_ITEM_FACE,
    MENU_ITEM_WIFI,
    MENU_ITEM_DELETE_SENT,
    MENU_ITEM_DELETE_ALL,
    MENU_ITEM_INFO,
    MENU_ITEM_SHUTDOWN,
    MENU_ITEM_COUNT
} menu_item_t;

/** Return a human-readable label for a menu item. */
const char *menu_item_label(menu_item_t item);

/* ── Menu actions ────────────────────────────────────────────────────── */

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_START_RECORDING,    /**< User chose "New Recording"       */
    MENU_ACTION_ENTER_FACE_SUBMENU, /**< User chose "Face"                */
    MENU_ACTION_SEND_ALL,           /**< User chose "Send All"            */
    MENU_ACTION_TOGGLE_WIFI,        /**< User chose "Wifi" — flip on/off  */
    MENU_ACTION_BACK_TO_HOME,       /**< User pressed back/left from menu */
    MENU_ACTION_SHOW_STUB,          /**< Stub screen (Info/etc.)          */
} menu_action_t;

/* ── Menu state ──────────────────────────────────────────────────────── */

typedef struct menu_state_s {
    menu_item_t cursor;     /**< Currently highlighted item               */
} menu_state_t;

/** Reset menu state to the first item. */
void menu_state_init(menu_state_t *state);

/**
 * @brief Pure navigation function for the main menu.
 *
 * @p state  Current state (modified in place).
 * @p button  Button pressed.
 * @p[out] out_action  Action the caller should take.
 */
void menu_navigate(menu_state_t *state, ButtonId button,
                   menu_action_t *out_action);

/* ── Face submenu state ──────────────────────────────────────────────── */

typedef struct face_submenu_state_s {
    int cursor;         /**< Currently highlighted theme index (0-based)  */
    int theme_count;    /**< Total number of registered themes            */
} face_submenu_state_t;

/** Reset face submenu state.  @p theme_count must be > 0. */
void face_submenu_state_init(face_submenu_state_t *state, int theme_count);

/**
 * @brief Pure navigation function for the Face submenu.
 *
 * @p state  Current state (modified in place).
 * @p button  Button pressed.
 * @p[out] out_theme_index  If CENTER pressed, the index of the selected theme.
 *                          Set to -1 if no selection was made.
 * @p[out] should_exit  Set to true if user pressed LEFT (back to main menu).
 */
void face_submenu_navigate(face_submenu_state_t *state, ButtonId button,
                           int *out_theme_index, bool *should_exit);

/* ── Renderers (called by ui_task) ───────────────────────────────────── */

/**
 * @brief Draw the main menu screen.
 *
 * Displays a scrolling list of 9 menu items with cursor highlight.
 * The face theme continues to draw in a small inset area.
 *
 * @param state     Current menu cursor position.
 * @param wifi_on   Current Wi-Fi radio state, shown on the Wifi row.
 */
void menu_screen_draw(const menu_state_t *state, bool wifi_on);

/**
 * @brief Draw the Face submenu screen.
 *
 * Lists the registered themes with cursor highlight.
 *
 * @param state  Current face submenu cursor position.
 */
void face_submenu_screen_draw(const face_submenu_state_t *state);

#ifdef __cplusplus
}
#endif
