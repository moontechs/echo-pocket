/** @file menu.c
 * @brief Main menu and Face submenu — pure navigation state machines
 *        and rendering functions.
 *
 * Layout (240×240):
 *   Row   0–23  Title bar ("Menu" or "Face Theme")
 *   Row  24–215 Menu item list (scrolling if needed)
 *   Row 216–239 Help bar (button hints)
 */

#include "menu.h"
#include "display.h"
#include "face_registry.h"
#include "ui_colors.h"

#include <stdio.h>
#include <string.h>

#define TITLE_BAR_H        24
#define HELP_BAR_H         24
#define HELP_BAR_Y         (240 - HELP_BAR_H)
#define LIST_TOP_Y         TITLE_BAR_H
#define LIST_BOTTOM_Y      HELP_BAR_Y
#define LIST_ITEM_H        20

/* ── Menu item labels ────────────────────────────────────────────────── */

static const char *s_menu_labels[MENU_ITEM_COUNT] = {
    "New Recording",
    "Recordings",
    "Unsent",
    "Send All",
    "Face",
    "Wi-Fi",
    "Telegram",
    "Settings",
    "Info",
};

const char *menu_item_label(menu_item_t item)
{
    if (item < 0 || item >= MENU_ITEM_COUNT) return "?";
    return s_menu_labels[item];
}

/* ── Menu state machine ──────────────────────────────────────────────── */

void menu_state_init(menu_state_t *state)
{
    if (!state) return;
    state->cursor = MENU_ITEM_NEW_RECORDING;
}

void menu_navigate(menu_state_t *state, ButtonId button,
                   menu_action_t *out_action)
{
    if (!state || !out_action) return;

    *out_action = MENU_ACTION_NONE;

    switch (button) {
    case BUTTON_LEFT:
        /* Back to home */
        *out_action = MENU_ACTION_BACK_TO_HOME;
        return;

    case BUTTON_RIGHT:
        /* Move cursor down / wrap around */
        if (state->cursor < MENU_ITEM_COUNT - 1) {
            state->cursor = (menu_item_t)((int)state->cursor + 1);
        } else {
            state->cursor = MENU_ITEM_NEW_RECORDING;
        }
        break;

    case BUTTON_CENTER:
        /* Select current item */
        switch (state->cursor) {
        case MENU_ITEM_NEW_RECORDING:
            *out_action = MENU_ACTION_START_RECORDING;
            break;
        case MENU_ITEM_FACE:
            *out_action = MENU_ACTION_ENTER_FACE_SUBMENU;
            break;
        case MENU_ITEM_SEND_ALL:
            *out_action = MENU_ACTION_SEND_ALL;
            break;
        default:
            /* Recordings, Unsent, Wi-Fi, Telegram, Settings, Info — stubs */
            *out_action = MENU_ACTION_SHOW_STUB;
            break;
        }
        return;

    default:
        break;
    }
}

/* ── Face submenu state machine ──────────────────────────────────────── */

void face_submenu_state_init(face_submenu_state_t *state, int theme_count)
{
    if (!state) return;
    state->cursor = 0;
    state->theme_count = (theme_count > 0) ? theme_count : 1;
}

void face_submenu_navigate(face_submenu_state_t *state, ButtonId button,
                           int *out_theme_index, bool *should_exit)
{
    if (!state || !out_theme_index || !should_exit) return;

    *out_theme_index = -1;
    *should_exit = false;

    switch (button) {
    case BUTTON_LEFT:
        /* Back to main menu */
        *should_exit = true;
        return;

    case BUTTON_RIGHT:
        /* Move cursor down / wrap */
        if (state->cursor < state->theme_count - 1) {
            state->cursor++;
        } else {
            state->cursor = 0;
        }
        break;

    case BUTTON_CENTER:
        /* Select the highlighted theme */
        *out_theme_index = state->cursor;
        return;

    default:
        break;
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Draw the title bar at the top of the screen. */
static void draw_title_bar(const char *title)
{
    display_fill_rect(0, 0, 240, TITLE_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, TITLE_BAR_H - 1, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, 4, title, UI_COLOR_TEXT);
}

/** Draw the help bar at the bottom. */
static void draw_help_bar(const char *left_label, const char *center_label,
                          const char *right_label)
{
    display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, HELP_BAR_Y, 240, UI_COLOR_TEXT_DIM);

    if (left_label) {
        display_draw_text(4, HELP_BAR_Y + 5, left_label, UI_COLOR_TEXT_DIM);
    }
    if (right_label) {
        int rw = (int)strlen(right_label) * 8;
        display_draw_text(240 - rw - 4, HELP_BAR_Y + 5, right_label, UI_COLOR_TEXT_DIM);
    }
    if (center_label) {
        int cw = (int)strlen(center_label) * 8;
        display_draw_text((240 - cw) / 2, HELP_BAR_Y + 5, center_label, UI_COLOR_TEXT);
    }
}

/** Draw a single menu row at the given pixel y position. */
static void draw_menu_row(int y, const char *label, bool selected)
{
    uint16_t bg_color = selected ? UI_COLOR_SELECT_BG : UI_COLOR_VOID;
    uint16_t fg_color = selected ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;

    display_fill_rect(0, y, 240, LIST_ITEM_H, bg_color);

    if (selected) {
        /* Draw cursor indicator arrow */
        display_draw_text(4, y + 2, ">", UI_COLOR_ACCENT_AMBER);
        display_draw_text(16, y + 2, label, fg_color);
    } else {
        display_draw_text(8, y + 2, label, fg_color);
    }
}

/* ── Menu screen renderer ────────────────────────────────────────────── */

void menu_screen_draw(const menu_state_t *state)
{
    if (!state) return;

    /* Clear background */
    display_clear(UI_COLOR_VOID);

    draw_title_bar("Menu");
    draw_help_bar("Back", "Select", "Next");

    /* Draw menu items with scrolling if needed.
     * Fixed viewport shows up to 9 items (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H.
     */
    int visible_items = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H;
    if (visible_items > MENU_ITEM_COUNT) visible_items = MENU_ITEM_COUNT;
    if (visible_items < 1) visible_items = 1;

    /* Determine scroll offset so cursor stays visible */
    int scroll = 0;
    if (state->cursor >= visible_items) {
        scroll = (int)state->cursor - visible_items + 1;
    }

    for (int i = 0; i < visible_items; i++) {
        int item_idx = scroll + i;
        if (item_idx >= MENU_ITEM_COUNT) break;

        int y = LIST_TOP_Y + i * LIST_ITEM_H;
        draw_menu_row(y, s_menu_labels[item_idx],
                      item_idx == (int)state->cursor);
    }
}

/* ── Face submenu renderer ───────────────────────────────────────────── */

void face_submenu_screen_draw(const face_submenu_state_t *state)
{
    if (!state) return;

    display_clear(UI_COLOR_VOID);

    draw_title_bar("Face Theme");
    draw_help_bar("Back", "Select", "Next");

    int visible_items = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_H;
    if (visible_items > state->theme_count) visible_items = state->theme_count;
    if (visible_items < 1) visible_items = 1;

    /* Determine scroll offset */
    int scroll = 0;
    if (state->cursor >= visible_items) {
        scroll = state->cursor - visible_items + 1;
    }
    if (scroll > state->theme_count - visible_items) {
        scroll = state->theme_count - visible_items;
    }
    if (scroll < 0) scroll = 0;

    /* Get active theme id for the [active] marker */
    const char *active_id = face_registry_get_active_id();

    for (int i = 0; i < visible_items; i++) {
        int theme_idx = scroll + i;
        if (theme_idx >= state->theme_count) break;

        const char *name = face_registry_get_display_name_by_index(theme_idx);
        if (!name) name = "?";

        /* Build label with "[active]" marker */
        char label[64];
        const char *theme_id = face_registry_get_id_by_index(theme_idx);
        if (theme_id && active_id && strcmp(theme_id, active_id) == 0) {
            snprintf(label, sizeof(label), "%s [active]", name);
        } else {
            snprintf(label, sizeof(label), "%s", name);
        }

        int y = LIST_TOP_Y + i * LIST_ITEM_H;
        draw_menu_row(y, label, theme_idx == state->cursor);
    }
}
