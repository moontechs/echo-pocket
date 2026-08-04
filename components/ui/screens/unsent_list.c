/** @file unsent_list.c
 * @brief Unsent list screen — paged list of queue entries in
 *        pending/failed state.
 *
 * Reads from queue_store (wired once Task 15 lands).  Until then,
 * the list is always empty and shows a "No unsent recordings" message.
 *
 * Layout (240×240):
 *   Row   0–23  Title bar ("Unsent")
 *   Row  24–215 List items (scrolling if needed)
 *   Row 216–239 Help bar (button hints + Send All action)
 *
 * Left   = back to menu
 * Right  = move cursor down / next
 * Center = Send All (triggers manual drain — wired once Task 17 exists)
 *
 * TODO(task-15): replace the empty-list stub with real queue_store reads.
 */

#include "list_screens.h"
#include "display.h"

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

/* ── Colour palette ─────────────────────────────────────────────────── */

#define COLOR_BLACK             ((uint16_t)0x0000)
#define COLOR_WHITE             ((uint16_t)0xFFFF)
#define COLOR_DARK_BG           ((uint16_t)0x18E3)
#define COLOR_BLUE              ((uint16_t)0x001F)
#define COLOR_GREY              ((uint16_t)0x8410)
#define COLOR_YELLOW            ((uint16_t)0xFFE0)
#define COLOR_RED               ((uint16_t)0xF800)

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
    display_fill_rect(0, 0, 240, TITLE_BAR_H, COLOR_DARK_BG);
    display_draw_hline(0, TITLE_BAR_H - 1, 240, COLOR_GREY);
    display_draw_text(4, 4, title, COLOR_WHITE);
}

static void draw_help_bar(void)
{
    display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, COLOR_DARK_BG);
    display_draw_hline(0, HELP_BAR_Y, 240, COLOR_GREY);
    display_draw_text(4, HELP_BAR_Y + 5, "Back", COLOR_GREY);

    /* Center: Send All (action dispatched by ui_task) */
    display_draw_text(240 - 8 * 4 - 4, HELP_BAR_Y + 5, "Next", COLOR_GREY);

    if (s_item_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d/%d",
                 s_pagination.cursor + 1, s_pagination.total_items);
        int cw = (int)strlen(buf) * 8;
        display_draw_text((240 - cw) / 2, HELP_BAR_Y + 5, buf, COLOR_WHITE);
    }
}

static void draw_row(int y, const unsent_item_t *item, bool selected)
{
    uint16_t bg_color = selected ? COLOR_BLUE : COLOR_BLACK;
    uint16_t fg_color = selected ? COLOR_WHITE : COLOR_GREY;

    display_fill_rect(0, y, 240, LIST_ITEM_H, bg_color);

    /* Build display string: "REC_ID  [state]  xN attempts" */
    char label[64];
    if (item->attempts > 0) {
        snprintf(label, sizeof(label), "%s  [%s]  x%d",
                 item->rec_id, item->state_str, item->attempts);
    } else {
        snprintf(label, sizeof(label), "%s  [%s]",
                 item->rec_id, item->state_str);
    }

    if (selected) {
        display_draw_text(4, y + 2, ">", COLOR_YELLOW);
        display_draw_text(16, y + 2, label, fg_color);
    } else {
        display_draw_text(8, y + 2, label, fg_color);
    }
}

/* ── TODO(task-15): replace with real queue_store read ─────────────── */

/**
 * Stub: read unsent queue entries from the queue store.
 *
 * Until Task 15 creates queue_store, this always returns 0 items.
 * When wired, it should populate s_items[] from
 * /echo-pocket/queue/index.json entries in "pending" or "failed" state.
 */
static void read_unsent_queue(void)
{
    s_item_count = 0;

    /* TODO(task-15): call queue_store_read_pending_and_failed()
     * to populate s_items[] and s_item_count.
     *
     * Example (after Task 15):
     *   queue_entry_t entries[MAX_UNSENT_ITEMS];
     *   int count = queue_store_get_unsent(entries, MAX_UNSENT_ITEMS);
     *   for (int i = 0; i < count; i++) {
     *       strncpy(s_items[i].rec_id, entries[i].id, sizeof(…)-1);
     *       s_items[i].state_str = queue_state_name(entries[i].state);
     *       s_items[i].attempts = entries[i].attempts;
     *   }
     *   s_item_count = count;
     */
}

/* ── Public API ──────────────────────────────────────────────────────── */

void unsent_list_enter(void)
{
    /* Read queue (stubbed until Task 15) */
    read_unsent_queue();

    /* Pagination init */
    int visible = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H;
    if (visible < 1) visible = 1;
    list_pagination_init(&s_pagination, s_item_count, visible);
}

void unsent_list_screen_draw(void)
{
    /* Draw only — no re-read.  unsent_list_enter() must be called
     * before the first draw when entering the screen. */
    display_clear(COLOR_BLACK);
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
                          COLOR_GREY);
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
