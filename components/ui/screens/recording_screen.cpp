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

#include <stdio.h>
#include "face_plugin.hpp"

/* ── Colour palette ──────────────────────────────────────────────────── */

#define COLOR_BLACK       ((uint16_t)0x0000)
#define COLOR_WHITE       ((uint16_t)0xFFFF)
#define COLOR_DARK_BG     ((uint16_t)0x18E3)
#define COLOR_RED          ((uint16_t)0xF800)
#define COLOR_GREEN        ((uint16_t)0x07E0)
#define COLOR_GREY         ((uint16_t)0x8410)

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
        display_clear(COLOR_BLACK);
    }

    /* 2. Overlay bottom bar on top of the face */
    display_fill_rect(0, BOTTOM_BAR_Y, 240, BOTTOM_BAR_H, COLOR_DARK_BG);
    display_draw_hline(0, BOTTOM_BAR_Y, 240, COLOR_GREY);

    if (status->show_saved) {
        /* "Saved" state — briefly shown after stop + fsync */
        display_draw_text(8, BOTTOM_BAR_Y + 8, "Saved", COLOR_GREEN);
        display_draw_text(120, BOTTOM_BAR_Y + 8, "OK", COLOR_GREEN);
    } else {
        /* Normal recording state */
        /* REC indicator (blinking dot + text) */
        display_fill_circle(12, BOTTOM_BAR_Y + 16, 4, COLOR_RED);
        display_draw_text(22, BOTTOM_BAR_Y + 8, "REC", COLOR_RED);

        /* Elapsed timer */
        char timer_buf[16];
        format_elapsed(status->recording_elapsed_ms, timer_buf, sizeof(timer_buf));
        display_draw_text(100, BOTTOM_BAR_Y + 8, timer_buf, COLOR_WHITE);
    }
}
