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

#include <stdio.h>
#include "face_plugin.hpp"

/* ── Colour palette ──────────────────────────────────────────────────── */

#define COLOR_BLACK       ((uint16_t)0x0000)
#define COLOR_WHITE       ((uint16_t)0xFFFF)
#define COLOR_DARK_BG     ((uint16_t)0x18E3)  /* dark blue-grey */
#define COLOR_GREEN       ((uint16_t)0x07E0)
#define COLOR_RED         ((uint16_t)0xF800)
#define COLOR_YELLOW      ((uint16_t)0xFFE0)
#define COLOR_GREY        ((uint16_t)0x8410)
#define COLOR_ORANGE      ((uint16_t)0xFD20)
#define COLOR_CYAN        ((uint16_t)0x07FF)

#define STATUS_BAR_H      24

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Draw a small icon + label pair at position.
 *  Icon is an optional 2-char text label; if NULL, skips icon. */
static void draw_status_item(int x, int y, const char *label,
                             uint16_t label_color, const char *value,
                             uint16_t value_color)
{
    if (label) {
        display_draw_text(x, y, label, label_color);
        x += (int)strlen(label) * 8 + 4;
    }
    if (value) {
        display_draw_text(x, y, value, value_color);
    }
}

/** Fill the status bar background. */
static void draw_status_bar_bg(void)
{
    display_fill_rect(0, 0, 240, STATUS_BAR_H, COLOR_DARK_BG);
    display_draw_hline(0, STATUS_BAR_H - 1, 240, COLOR_GREY);
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
        display_clear(COLOR_BLACK);
    }

    /* 2. Overlay status bar on top */
    draw_status_bar_bg();

    char buf[32];
    int x = 2;

    /* ── Wi-Fi indicator ─────────────────────────────────────── */
    const char *wifi_label = status->wifi_connected ? "W" : "w";
    uint16_t wifi_color = status->wifi_connected ? COLOR_GREEN : COLOR_RED;
    display_draw_text(x, 4, wifi_label, wifi_color);
    x += 14;

    /* ── SD card indicator ───────────────────────────────────── */
    const char *sd_label = status->sd_mounted ? "SD" : "sd";
    uint16_t sd_color = status->sd_mounted ? COLOR_GREEN : COLOR_RED;
    display_draw_text(x, 4, sd_label, sd_color);
    x += 24;

    /* ── Pending uploads ─────────────────────────────────────── */
    if (status->pending_uploads > 0) {
        snprintf(buf, sizeof(buf), "Q:%d", status->pending_uploads);
        display_draw_text(x, 4, buf, COLOR_YELLOW);
        x += (int)strlen(buf) * 8 + 6;
    }

    /* ── Battery ─────────────────────────────────────────────── */
    if (status->battery_present && status->battery_percent >= 0) {
        uint16_t bat_color;
        if (status->battery_percent <= 10) {
            bat_color = COLOR_RED;
        } else if (status->battery_percent <= 20) {
            bat_color = COLOR_ORANGE;
        } else {
            bat_color = COLOR_GREEN;
        }
        if (status->charging) {
            snprintf(buf, sizeof(buf), "B:%d%%+", status->battery_percent);
        } else {
            snprintf(buf, sizeof(buf), "B:%d%%", status->battery_percent);
        }
        display_draw_text(x, 4, buf, bat_color);
    } else {
        /* Battery unknown — show placeholder (Task 18 will replace) */
        display_draw_text(x, 4, "B:--", COLOR_GREY);
    }
}
