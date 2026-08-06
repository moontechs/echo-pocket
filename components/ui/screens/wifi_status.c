/** @file wifi_status.c
 * @brief Wi-Fi status screen — shows connection state, SSID, and IP.
 *
 * Layout (240×240):
 *   Row   0–23  Title bar ("Wi-Fi")
 *   Row  24–215 Status lines
 *   Row 216–239 Help bar (button hints)
 *
 * Left = back to menu. Right/Center = no-op (nothing to select).
 */

#include "ui_task.h"
#include "display.h"
#include "wifi_manager.h"
#include "ui_colors.h"

#include <stdio.h>

#define TITLE_BAR_H     24
#define HELP_BAR_H      24
#define HELP_BAR_Y      (240 - HELP_BAR_H)
#define LIST_TOP_Y      TITLE_BAR_H
#define LINE_H          20

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
}

void wifi_status_screen_draw(void)
{
    display_clear(UI_COLOR_VOID);
    draw_title_bar("Wi-Fi");
    draw_help_bar();

    bool connected = wifi_manager_is_connected();
    char ssid[33] = {0};
    char ip[16] = {0};
    wifi_manager_get_status(ssid, sizeof(ssid), ip, sizeof(ip));

    int y = LIST_TOP_Y + 8;
    display_draw_text(8, y, connected ? "Status: Connected" : "Status: Disconnected",
                      connected ? UI_COLOR_ACCENT_MINT : UI_COLOR_ACCENT_AMBER);
    y += LINE_H;

    char line[48];
    snprintf(line, sizeof(line), "SSID: %s", ssid[0] ? ssid : "-");
    display_draw_text(8, y, line, UI_COLOR_TEXT);
    y += LINE_H;

    snprintf(line, sizeof(line), "IP: %s", ip[0] ? ip : "-");
    display_draw_text(8, y, line, UI_COLOR_TEXT);
}
