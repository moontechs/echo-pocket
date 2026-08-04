/** @file test_list_pagination.c
 * @brief Unit tests for the pure list pagination state machine
 *        used by Recordings and Unsent list screens.
 *
 * Verifies:
 *   - init with various item counts
 *   - scroll offset calculation for different cursor positions
 *   - cursor down/up wrapping
 *   - edge cases (0 items, 1 item, null safety)
 */

#include "unity.h"
#include "list_screens.h"

/* ── Initialisation ──────────────────────────────────────────────────── */

void test_pagination_init_normal(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 10, 5);
    TEST_ASSERT_EQUAL(10, p.total_items);
    TEST_ASSERT_EQUAL(5,  p.page_size);
    TEST_ASSERT_EQUAL(0,  p.cursor);
}

void test_pagination_init_zero_items(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 0, 5);
    TEST_ASSERT_EQUAL(0, p.total_items);
    TEST_ASSERT_EQUAL(5, p.page_size);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_init_negative_items(void)
{
    list_pagination_t p;
    list_pagination_init(&p, -1, 5);
    TEST_ASSERT_EQUAL(0, p.total_items);
    TEST_ASSERT_EQUAL(5, p.page_size);
}

void test_pagination_init_zero_page_size(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 10, 0);
    /* page_size 0 is clamped to 1 */
    TEST_ASSERT_EQUAL(10, p.total_items);
    TEST_ASSERT_EQUAL(1,  p.page_size);
}

void test_pagination_init_negative_page_size(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 10, -3);
    TEST_ASSERT_EQUAL(10, p.total_items);
    TEST_ASSERT_EQUAL(1,  p.page_size);
}

void test_pagination_init_null(void)
{
    /* Should not crash */
    list_pagination_init(NULL, 10, 5);
}

/* ── Scroll offset ───────────────────────────────────────────────────── */

void test_pagination_scroll_items_fewer_than_page(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 3, 10);  /* 3 items, 10 per page */
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));

    /* Cursor at last item — still offset 0 */
    p.cursor = 2;
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_items_equal_to_page(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 5, 5);
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));

    p.cursor = 4;  /* last item, still visible */
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_cursor_in_first_page(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 20, 5);

    /* Cursors 0-4 are all in the first visible page */
    for (int c = 0; c < 5; c++) {
        p.cursor = c;
        TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));
    }
}

void test_pagination_scroll_cursor_middle(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 20, 5);

    /* Cursor at index 7 — should be centered-ish */
    p.cursor = 7;
    /* visible=5, center of page = 2.  So offset = 7 - 2 = 5 */
    TEST_ASSERT_EQUAL(5, list_pagination_scroll_offset(&p));

    /* Cursor at index 10 */
    p.cursor = 10;
    TEST_ASSERT_EQUAL(8, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_cursor_near_end(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 20, 5);

    /* Cursor at index 16: total-visible = 15, cursor >= 15 */
    p.cursor = 16;
    TEST_ASSERT_EQUAL(15, list_pagination_scroll_offset(&p));

    /* Cursor at index 19 (last) */
    p.cursor = 19;
    TEST_ASSERT_EQUAL(15, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_one_item(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 1, 5);
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_zero_items(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 0, 5);
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(&p));
}

void test_pagination_scroll_null(void)
{
    TEST_ASSERT_EQUAL(0, list_pagination_scroll_offset(NULL));
}

/* ── Cursor down ─────────────────────────────────────────────────────── */

void test_pagination_cursor_down_basic(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 5, 3);

    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(1, p.cursor);

    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(2, p.cursor);

    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(3, p.cursor);

    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(4, p.cursor);
}

void test_pagination_cursor_down_wraps(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 3, 3);

    /* Move to last item */
    list_pagination_cursor_down(&p);
    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(2, p.cursor);

    /* Wrap to top */
    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_cursor_down_single_item(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 1, 5);

    /* Wrap immediately back to 0 */
    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_cursor_down_zero_items(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 0, 5);
    list_pagination_cursor_down(&p);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_cursor_down_null(void)
{
    /* Should not crash */
    list_pagination_cursor_down(NULL);
}

/* ── Cursor up ───────────────────────────────────────────────────────── */

void test_pagination_cursor_up_basic(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 5, 3);
    p.cursor = 4;

    list_pagination_cursor_up(&p);
    TEST_ASSERT_EQUAL(3, p.cursor);

    list_pagination_cursor_up(&p);
    TEST_ASSERT_EQUAL(2, p.cursor);
}

void test_pagination_cursor_up_wraps(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 3, 3);
    /* cursor at 0 → wrap to last (2) */
    list_pagination_cursor_up(&p);
    TEST_ASSERT_EQUAL(2, p.cursor);
}

void test_pagination_cursor_up_single_item(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 1, 5);
    list_pagination_cursor_up(&p);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_cursor_up_zero_items(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 0, 5);
    list_pagination_cursor_up(&p);
    TEST_ASSERT_EQUAL(0, p.cursor);
}

void test_pagination_cursor_up_null(void)
{
    list_pagination_cursor_up(NULL);
}

/* ── Full traversal ──────────────────────────────────────────────────── */

void test_pagination_full_traverse_down_and_back(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 10, 4);

    /* Traverse down through all items */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(i % 10, p.cursor);
        list_pagination_cursor_down(&p);
    }
    /* Wrapped back to 0 */
    TEST_ASSERT_EQUAL(0, p.cursor);

    /* Traverse up through all items */
    for (int i = 0; i < 10; i++) {
        list_pagination_cursor_up(&p);
    }
    /* Wrapped back to 0 */
    TEST_ASSERT_EQUAL(0, p.cursor);
}

/* ── Scroll offset: exact boundaries ─────────────────────────────────── */

void test_pagination_scroll_two_pages_exact(void)
{
    list_pagination_t p;
    list_pagination_init(&p, 10, 5);  /* 2 full pages */

    /* First page: cursors 0-4, offset 0 */
    for (int c = 0; c < 5; c++) {
        p.cursor = c;
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, list_pagination_scroll_offset(&p),
                                      "First page should have offset 0");
    }

    /* Second page: cursors 5-9, offset 5 */
    for (int c = 5; c < 10; c++) {
        p.cursor = c;
        TEST_ASSERT_EQUAL_INT_MESSAGE(5, list_pagination_scroll_offset(&p),
                                      "Second page should have offset 5");
    }
}
