/** @file unsent_list.c
 * @brief Unsent list screen — paged list of queue entries in
 *        pending/failed state.
 *
 * Layout (240×240):
 *   Row   0–23  Title bar ("Unsent")
 *   Row  24–215 List items (scrolling if needed)
 *   Row 216–239 Help bar (button hints + Send All action)
 *
 * Left   = back to menu
 * Right  = move cursor down / next
 * Center = Send All (triggers manual drain — wired once Task 17 exists)
 */

#include "list_screens.h"
#include "display.h"
#include "ui_colors.h"
#include "queue_store.h"

#include <stdio.h>
#include <string.h>

/* ── Maximum unsent items to track ───────────────────────────────────── */

#define MAX_UNSENT_ITEMS        200

/* Screen layout (mirrors menu.c and recordings_list.c). */
#define TITLE_BAR_H             24
#define HELP_BAR_H              24
#define HELP_BAR_Y              (240 - HELP_BAR_H)
#define LIST_TOP_Y              TITLE_BAR_H
#define LIST_BOTTOM_Y           HELP_BAR_Y
#define LIST_ITEM_H             20

/* ── Item type ───────────────────────────────────────────────────────── */

typedef struct {
    char rec_id[64];        /**< Recording ID (e.g. "REC_2025...") */
    char state_str[16];     /**< "pending" or "failed"             */
    int  attempts;           /**< Number of upload attempts         */
} unsent_item_t;

/* ── Static state ────────────────────────────────────────────────────── */

static unsent_item_t        s_items[MAX_UNSENT_ITEMS];
static int                  s_item_count = 0;
static list_pagination_t    s_pagination;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void draw_title_bar(const char *title)
{
    display_fill_rect(0, 0, 240, TITLE_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, TITLE_BAR_H - 1, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, 4, title, UI_COLOR_TEXT);
}

static void draw_help_bar(void)
{
    display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, HELP_BAR_Y, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, HELP_BAR_Y + 5, "Back", UI_COLOR_TEXT_DIM);

    /* Center: Send All (action dispatched by ui_task) */
    display_draw_text(240 - 8 * 4 - 4, HELP_BAR_Y + 5, "Next", UI_COLOR_TEXT_DIM);

    if (s_item_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d/%d",
                 s_pagination.cursor + 1, s_pagination.total_items);
        int cw = (int)strlen(buf) * 8;
        display_draw_text((240 - cw) / 2, HELP_BAR_Y + 5, buf, UI_COLOR_TEXT);
    }
}

static void draw_row(int y, const unsent_item_t *item, bool selected)
{
    uint16_t bg_color = selected ? UI_COLOR_SELECT_BG : UI_COLOR_VOID;
    uint16_t fg_color = selected ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;

    display_fill_rect(0, y, 240, LIST_ITEM_H, bg_color);

    /* Build display string: "REC_ID  [state]  xN attempts" */
    char label[100];
    if (item->attempts > 0) {
        snprintf(label, sizeof(label), "%s  [%s]  x%d",
                 item->rec_id, item->state_str, item->attempts);
    } else {
        snprintf(label, sizeof(label), "%s  [%s]",
                 item->rec_id, item->state_str);
    }

    if (selected) {
        display_draw_text(4, y + 2, ">", UI_COLOR_ACCENT_AMBER);
        display_draw_text(16, y + 2, label, fg_color);
    } else {
        display_draw_text(8, y + 2, label, fg_color);
    }
}

/** Read entries in PENDING or FAILED state from the queue store into
 *  s_items[]. */
static void read_unsent_queue(const queue_index_t *queue)
{
    s_item_count = 0;
    if (!queue) return;

    int count = 0;
    const queue_entry_t *entries = queue_store_get_entries(queue, &count);
    if (!entries) return;

    for (int i = 0; i < count && s_item_count < MAX_UNSENT_ITEMS; i++) {
        if (entries[i].state != QUEUE_STATE_PENDING &&
            entries[i].state != QUEUE_STATE_FAILED) {
            continue;
        }

        unsent_item_t *item = &s_items[s_item_count];
        strncpy(item->rec_id, entries[i].id, sizeof(item->rec_id) - 1);
        item->rec_id[sizeof(item->rec_id) - 1] = '\0';
        strncpy(item->state_str, queue_state_str(entries[i].state),
                sizeof(item->state_str) - 1);
        item->state_str[sizeof(item->state_str) - 1] = '\0';
        item->attempts = entries[i].attempts;
        s_item_count++;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void unsent_list_enter(const queue_index_t *queue)
{
    read_unsent_queue(queue);

    /* Pagination init */
    int visible = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H;
    if (visible < 1) visible = 1;
    list_pagination_init(&s_pagination, s_item_count, visible);
}

int unsent_list_get_item_count(void)
{
    return s_item_count;
}

void unsent_list_screen_draw(void)
{
    /* Draw only — no re-read.  unsent_list_enter() must be called
     * before the first draw when entering the screen. */
    display_clear(UI_COLOR_VOID);
    draw_title_bar("Unsent");
    draw_help_bar();

    int scroll = list_pagination_scroll_offset(&s_pagination);
    int visible = s_pagination.page_size;

    for (int i = 0; i < visible; i++) {
        int item_idx = scroll + i;
        if (item_idx >= s_item_count) break;

        int y = LIST_TOP_Y + i * LIST_ITEM_H;
        draw_row(y, &s_items[item_idx],
                 item_idx == s_pagination.cursor);
    }

    if (s_item_count == 0) {
        display_draw_text(8, LIST_TOP_Y + 20, "No unsent recordings",
                          UI_COLOR_TEXT_DIM);
    }
}

void unsent_list_navigate(ButtonId button, bool *should_exit)
{
    if (!should_exit) return;
    *should_exit = false;

    switch (button) {
    case BUTTON_LEFT:
        *should_exit = true;
        break;

    case BUTTON_RIGHT:
        list_pagination_cursor_down(&s_pagination);
        break;

    case BUTTON_CENTER:
        /* CENTER dispatches "Send All" — handled by ui_task
         * since it maps the button to a UI action.  This function
         * only tells ui_task "button was pressed" and ui_task
         * dispatches accordingly. */
        break;

    default:
        break;
    }
}
