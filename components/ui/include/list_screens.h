/** @file list_screens.h
 * @brief Paged list screen types, pure pagination state machine,
 *        and renderer declarations for Recordings and Unsent screens.
 *
 * Recordings: paged list of /echo-pocket/rec/ .wav files (browse only).
 * Unsent:     paged list of queue entries in pending/failed state.
 *
 * Send All:   no separate screen — dispatched directly from the
 *             main menu via MENU_ACTION_SEND_ALL (already wired).
 */

#pragma once

#include "buttons.h"
#include "queue_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── List pagination (pure logic — unit-testable) ──────────────────── */

typedef struct {
    int total_items;    /**< Total number of items in the list         */
    int page_size;      /**< Number of items visible per page          */
    int cursor;         /**< 0-based index into total_items            */
} list_pagination_t;

/** Initialise pagination state.  Safe to call with total_items <= 0. */
void list_pagination_init(list_pagination_t *p, int total_items,
                          int page_size);

/**
 * @brief Return the index of the first visible item on the current page
 *        (the scroll offset).  0-based.
 */
int list_pagination_scroll_offset(const list_pagination_t *p);

/** Move cursor down (wrap to top when past end). */
void list_pagination_cursor_down(list_pagination_t *p);

/** Move cursor up (wrap to bottom when above start). */
void list_pagination_cursor_up(list_pagination_t *p);

/* ── Screen renderers (called by ui_task only) ──────────────────────── */

/** Draw the Recordings list screen. */
void recordings_list_screen_draw(void);

/** Handle a button press while on the Recordings list screen.
 *  @param[out] should_exit  Set to true if LEFT was pressed (back to menu). */
void recordings_list_navigate(ButtonId button, bool *should_exit);

/** Called when entering the Recordings list screen — scans SD once. */
void recordings_list_enter(void);

/** Draw the Unsent list screen. */
void unsent_list_screen_draw(void);

/** Handle a button press while on the Unsent list screen.
 *  @param[out] should_exit  Set to true if LEFT was pressed (back to menu). */
void unsent_list_navigate(ButtonId button, bool *should_exit);

/** Called when entering the Unsent list screen — reads queue once. */
void unsent_list_enter(const queue_index_t *queue);

/** Number of unsent items read by the last unsent_list_enter() call. */
int unsent_list_get_item_count(void);

#ifdef __cplusplus
}
#endif
