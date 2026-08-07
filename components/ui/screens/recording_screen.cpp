/** @file recording_screen.c
 * @brief Recording screen renderer — face area + plain elapsed timer.
 *
 * The face itself carries the state word ("REC", "SAVING", ...) via its
 * shared event label (face_event_label()); this screen only adds the
 * elapsed-time readout, drawn directly over the face with no bar/box.
 */

#include "display.h"
#include "ui_task.h"
#include "face_registry.h"
#include "ui_colors.h"

#include <stdio.h>
#include "face_plugin.hpp"

#define TIMER_X   8
#define TIMER_Y   8

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

    /* 2. Elapsed timer, plain text, no background — the face's own label
     *    already says "REC" / "SAVING" via face_event_label(). */
    if (!status->show_saved) {
        char timer_buf[16];
        format_elapsed(status->recording_elapsed_ms, timer_buf, sizeof(timer_buf));
        display_draw_text(TIMER_X, TIMER_Y, timer_buf, UI_COLOR_TEXT);
    }
}
