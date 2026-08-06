/** @file home_screen.c
 * @brief Home screen renderer — face area + top status bar.
 *
 * Layout (240×240):
 *   Row  0–23  Status bar (dark background)
 *   Row 24–239 Face area (theme draws here)
 *
 * The status bar shows Wi-Fi, SD card, pending-upload count, and battery
 * placeholder.  Face theme drawing fills the remainder.
 */

#include "display.h"
#include "ui_task.h"
#include "face_registry.h"
#include "ui_colors.h"

#include <cstdio>
#include <cstring>
#include "face_plugin.hpp"

#define STATUS_BAR_H      24

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Fill the status bar background. */
static void draw_status_bar_bg(void)
{
    display_fill_rect(0, 0, 240, STATUS_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, STATUS_BAR_H - 1, 240, UI_COLOR_HAIRLINE);
}

/** True if anything in the status bar needs the user's attention —
 *  the bar stays hidden otherwise so the face owns the full screen. */
static bool status_needs_attention(const ui_status_t *status)
{
    if (!status->wifi_connected) return true;
    if (!status->sd_mounted) return true;
    if (status->pending_uploads > 0) return true;
    if (status->battery_present && status->battery_percent >= 0 &&
        status->battery_percent <= 20) {
        return true;
    }
    return false;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void home_screen_draw(const ui_status_t *status)
{
    if (!status) return;

    /* 1. Let the face theme draw first (full screen) */
    FacePlugin *face = face_registry_get_active();
    if (face) {
        face->draw();
    } else {
        /* Fallback: clear to black if no face is active */
        display_clear(UI_COLOR_VOID);
    }

    /* 2. Ambient status bar — only surfaces when something needs attention */
    if (!status_needs_attention(status)) return;

    draw_status_bar_bg();

    char buf[32];
    int x = 2;

    /* ── Wi-Fi indicator ─────────────────────────────────────── */
    uint16_t wifi_color = status->wifi_connected ? UI_COLOR_ACCENT_MINT : UI_COLOR_ACCENT_CORAL;
    display_draw_text(x, 4, "WIFI", wifi_color);
    x += 4 * 8 + 6;

    /* ── SD card indicator ───────────────────────────────────── */
    uint16_t sd_color = status->sd_mounted ? UI_COLOR_ACCENT_MINT : UI_COLOR_ACCENT_CORAL;
    display_draw_text(x, 4, "SD", sd_color);
    x += 2 * 8 + 8;

    /* ── Pending uploads ─────────────────────────────────────── */
    if (status->pending_uploads > 0) {
        snprintf(buf, sizeof(buf), "QUEUE:%d", status->pending_uploads);
        display_draw_text(x, 4, buf, UI_COLOR_ACCENT_AMBER);
        x += (int)strlen(buf) * 8 + 6;
    }

    /* ── Battery ─────────────────────────────────────────────── */
    if (status->battery_present && status->battery_percent >= 0) {
        uint16_t bat_color;
        if (status->battery_percent <= 10) {
            bat_color = UI_COLOR_ACCENT_CORAL;
        } else if (status->battery_percent <= 20) {
            bat_color = UI_COLOR_ACCENT_AMBER;
        } else {
            bat_color = UI_COLOR_ACCENT_MINT;
        }
        if (status->charging) {
            snprintf(buf, sizeof(buf), "BAT:%d%%+", status->battery_percent);
        } else {
            snprintf(buf, sizeof(buf), "BAT:%d%%", status->battery_percent);
        }
        display_draw_text(x, 4, buf, bat_color);
    } else {
        /* Battery unknown — show placeholder (Task 18 will replace) */
        display_draw_text(x, 4, "BAT:--", UI_COLOR_TEXT_DIM);
    }
}
