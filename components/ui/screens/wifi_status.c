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

#include <stdio.h>

#define TITLE_BAR_H     24
#define HELP_BAR_H      24
#define HELP_BAR_Y      (240 - HELP_BAR_H)
#define LIST_TOP_Y      TITLE_BAR_H
#define LINE_H          20

#define COLOR_BLACK     ((uint16_t)0x0000)
#define COLOR_WHITE     ((uint16_t)0xFFFF)
#define COLOR_DARK_BG   ((uint16_t)0x18E3)
#define COLOR_GREY      ((uint16_t)0x8410)
#define COLOR_GREEN     ((uint16_t)0x07E0)
#define COLOR_YELLOW    ((uint16_t)0xFFE0)

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
}

void wifi_status_screen_draw(void)
{
    display_clear(COLOR_BLACK);
    draw_title_bar("Wi-Fi");
    draw_help_bar();

    bool connected = wifi_manager_is_connected();
    char ssid[33] = {0};
    char ip[16] = {0};
    wifi_manager_get_status(ssid, sizeof(ssid), ip, sizeof(ip));

    int y = LIST_TOP_Y + 8;
    display_draw_text(8, y, connected ? "Status: Connected" : "Status: Disconnected",
                      connected ? COLOR_GREEN : COLOR_YELLOW);
    y += LINE_H;

    char line[48];
    snprintf(line, sizeof(line), "SSID: %s", ssid[0] ? ssid : "-");
    display_draw_text(8, y, line, COLOR_WHITE);
    y += LINE_H;

    snprintf(line, sizeof(line), "IP: %s", ip[0] ? ip : "-");
    display_draw_text(8, y, line, COLOR_WHITE);
}
