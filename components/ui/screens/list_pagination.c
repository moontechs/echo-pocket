/** @file list_pagination.c
 * @brief Pure pagination state machine for paged list screens.
 *
 * Used by both the Recordings list and the Unsent list.
 * Unit-testable without hardware, SD, or FreeRTOS.
 */

#include "list_screens.h"

/* ── Public API ──────────────────────────────────────────────────────── */

void list_pagination_init(list_pagination_t *p, int total_items,
                          int page_size)
{
    if (!p) return;

    p->total_items = (total_items < 0) ? 0 : total_items;
    p->page_size   = (page_size < 1)   ? 1 : page_size;
    p->cursor      = 0;
}

int list_pagination_scroll_offset(const list_pagination_t *p)
{
    if (!p || p->total_items <= 0) return 0;

    int visible = p->page_size;
    if (visible > p->total_items) visible = p->total_items;

    /* Keep cursor visible: scroll so the cursor is within the visible
     * window, preferring to show the cursor near the bottom unless
     * we're at the very beginning of the list. */
    if (p->cursor < visible) {
        return 0;
    }
    if (p->cursor >= p->total_items - visible) {
        int offset = p->total_items - visible;
        return (offset < 0) ? 0 : offset;
    }
    /* Cursor is in the middle — centre it in the visible window. */
    return p->cursor - (visible / 2);
}

void list_pagination_cursor_down(list_pagination_t *p)
{
    if (!p || p->total_items <= 0) return;

    if (p->cursor < p->total_items - 1) {
        p->cursor++;
    } else {
        p->cursor = 0;  /* wrap to top */
    }
}

void list_pagination_cursor_up(list_pagination_t *p)
{
    if (!p || p->total_items <= 0) return;

    if (p->cursor > 0) {
        p->cursor--;
    } else {
        p->cursor = p->total_items - 1;  /* wrap to bottom */
    }
}
