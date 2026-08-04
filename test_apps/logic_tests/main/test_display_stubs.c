/** @file test_display_stubs.c
 * @brief No-op display stubs for logic_tests — themes call display_* but
 *        the test app has no LCD hardware or esp_lcd component linked.
 *
 * These stubs count draw calls so the theme smoke tests can assert that
 * draw() actually produced output without crashing.
 */

#include <stdint.h>
#include <stddef.h>

/* ── Draw-call counters (extern so test_face_themes.cpp can read them) ─ */

int g_stub_clear_count   = 0;
int g_stub_text_count    = 0;
int g_stub_fill_rect_count = 0;
int g_stub_draw_rect_count = 0;
int g_stub_fill_circle_count = 0;
int g_stub_hline_count   = 0;

/* ── Reset counters between tests ────────────────────────────────────── */

void stub_display_reset_counters(void)
{
    g_stub_clear_count   = 0;
    g_stub_text_count    = 0;
    g_stub_fill_rect_count = 0;
    g_stub_draw_rect_count = 0;
    g_stub_fill_circle_count = 0;
    g_stub_hline_count   = 0;
}

/* ── No-op implementations ───────────────────────────────────────────── */

void display_clear(uint16_t color)
{
    (void)color;
    g_stub_clear_count++;
}

void display_draw_text(int x, int y, const char *text, uint16_t color)
{
    (void)x; (void)y; (void)text; (void)color;
    if (text) g_stub_text_count++;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    g_stub_fill_rect_count++;
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    g_stub_draw_rect_count++;
}

void display_fill_circle(int cx, int cy, int r, uint16_t color)
{
    (void)cx; (void)cy; (void)r; (void)color;
    g_stub_fill_circle_count++;
}

void display_draw_hline(int x, int y, int w, uint16_t color)
{
    (void)x; (void)y; (void)w; (void)color;
    g_stub_hline_count++;
}
