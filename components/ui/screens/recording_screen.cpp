/** @file recording_screen.c
 * @brief Recording screen renderer — face area + bottom REC bar with timer.
 *
 * Layout (240×240):
 *   Row   0–207  Face area (theme draws here)
 *   Row 208–239  Bottom bar: REC indicator + elapsed timer (HH:MM:SS)
 *
 * When status->show_saved is true, "Saved" is shown instead of REC.
 */

#include "display.h"
#include "ui_task.h"
#include "face_registry.h"
#include "ui_colors.h"

#include <stdio.h>
#include "face_plugin.hpp"

#define BOTTOM_BAR_H       32
#define BOTTOM_BAR_Y       (240 - BOTTOM_BAR_H)
#define FACE_AREA_H        BOTTOM_BAR_Y

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Format elapsed milliseconds as HH:MM:SS string. */
static void format_elapsed(uint32_t elapsed_ms, char *buf, size_t buf_size)
{
    uint32_t total_s = elapsed_ms / 1000;
    uint32_t h = total_s / 3600;
    uint32_t m = (total_s % 3600) / 60;
    uint32_t s = total_s % 60;

    if (h > 0) {
        snprintf(buf, buf_size, "%02lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        snprintf(buf, buf_size, "%02lu:%02lu",
                 (unsigned long)m, (unsigned long)s);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void recording_screen_draw(const ui_status_t *status)
{
    if (!status) return;

    /* 1. Let the face theme draw first (full screen) */
    FacePlugin *face = face_registry_get_active();
    if (face) {
        face->draw();
    } else {
        display_clear(UI_COLOR_VOID);
    }

    /* 2. Overlay bottom bar on top of the face */
    display_fill_rect(0, BOTTOM_BAR_Y, 240, BOTTOM_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, BOTTOM_BAR_Y, 240, UI_COLOR_HAIRLINE);

    if (status->show_saved) {
        /* "Saved" state — briefly shown after stop + fsync */
        display_draw_text(8, BOTTOM_BAR_Y + 8, "Saved", UI_COLOR_ACCENT_MINT);
        display_draw_text(120, BOTTOM_BAR_Y + 8, "OK", UI_COLOR_ACCENT_MINT);
    } else {
        /* Normal recording state */
        /* REC indicator (blinking dot + text) */
        display_fill_circle(12, BOTTOM_BAR_Y + 16, 4, UI_COLOR_ACCENT_CORAL);
        display_draw_text(22, BOTTOM_BAR_Y + 8, "REC", UI_COLOR_ACCENT_CORAL);

        /* Elapsed timer */
        char timer_buf[16];
        format_elapsed(status->recording_elapsed_ms, timer_buf, sizeof(timer_buf));
        display_draw_text(100, BOTTOM_BAR_Y + 8, timer_buf, UI_COLOR_TEXT);
    }
}
