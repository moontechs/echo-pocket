/** @file delete_confirm.c
 * @brief "Delete N recordings?" confirm screen, shared by the "Delete
 *        Sent" and "Delete All" menu items.
 *
 * Reached from the main menu.  Two-step flow, and Delete lives on a
 * different button (RIGHT) than the menu's own select button (CENTER),
 * so a fast double-press on the same button can never trigger a delete:
 *
 *   ASK    "Delete N sent recordings?" / "Delete ALL N recordings?"
 *          Left=Cancel  Right=Delete  Center=no-op
 *   RESULT "Deleted N files."           any button -> back to menu
 *
 * Layout (240×240): same title/help bar convention as info_screen.c.
 */

#include "ui_task.h"
#include "display.h"
#include "ui_colors.h"

#include <stdio.h>

#define TITLE_BAR_H     24
#define HELP_BAR_H      24
#define HELP_BAR_Y      (240 - HELP_BAR_H)
#define LIST_TOP_Y      TITLE_BAR_H

typedef enum {
    DELETE_CONFIRM_STATE_ASK = 0,
    DELETE_CONFIRM_STATE_RESULT,
} delete_confirm_state_t;

static delete_confirm_state_t s_state = DELETE_CONFIRM_STATE_ASK;
static delete_confirm_kind_t  s_kind = DELETE_CONFIRM_SENT;
static int s_item_count = 0;
static int s_deleted_count = 0;

static void draw_title_bar(const char *title)
{
    display_fill_rect(0, 0, 240, TITLE_BAR_H, UI_COLOR_INK);
    display_draw_hline(0, TITLE_BAR_H - 1, 240, UI_COLOR_TEXT_DIM);
    display_draw_text(4, 4, title, UI_COLOR_TEXT);
}

void delete_confirm_enter(delete_confirm_kind_t kind, int item_count)
{
    s_state = DELETE_CONFIRM_STATE_ASK;
    s_kind = kind;
    s_item_count = item_count;
    s_deleted_count = 0;
}

void delete_confirm_screen_draw(void)
{
    const char *title = (s_kind == DELETE_CONFIRM_ALL) ? "Delete All" : "Delete Sent";

    display_clear(UI_COLOR_VOID);
    draw_title_bar(title);

    char line[48];
    if (s_state == DELETE_CONFIRM_STATE_ASK) {
        if (s_item_count > 0) {
            if (s_kind == DELETE_CONFIRM_ALL) {
                snprintf(line, sizeof(line), "Delete ALL %d recording%s?",
                         s_item_count, s_item_count == 1 ? "" : "s");
            } else {
                snprintf(line, sizeof(line), "Delete %d sent recording%s?",
                         s_item_count, s_item_count == 1 ? "" : "s");
            }
        } else {
            snprintf(line, sizeof(line), "No recordings to delete.");
        }
        display_draw_text(8, LIST_TOP_Y + 8, line, UI_COLOR_TEXT);

        if (s_kind == DELETE_CONFIRM_ALL && s_item_count > 0) {
            display_draw_text(8, LIST_TOP_Y + 28, "Includes unsent/untracked",
                              UI_COLOR_ACCENT_AMBER);
        }

        display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, UI_COLOR_INK);
        display_draw_hline(0, HELP_BAR_Y, 240, UI_COLOR_TEXT_DIM);
        display_draw_text(4, HELP_BAR_Y + 5, "Cancel", UI_COLOR_TEXT_DIM);
        if (s_item_count > 0) {
            display_draw_text(240 - 8 * 6 - 4, HELP_BAR_Y + 5, "Delete",
                              UI_COLOR_ACCENT_CORAL);
        }
    } else {
        snprintf(line, sizeof(line), "Deleted %d file%s.",
                 s_deleted_count, s_deleted_count == 1 ? "" : "s");
        display_draw_text(8, LIST_TOP_Y + 8, line, UI_COLOR_TEXT);

        display_fill_rect(0, HELP_BAR_Y, 240, HELP_BAR_H, UI_COLOR_INK);
        display_draw_hline(0, HELP_BAR_Y, 240, UI_COLOR_TEXT_DIM);
        display_draw_text(4, HELP_BAR_Y + 5, "Back", UI_COLOR_TEXT_DIM);
    }
}

void delete_confirm_navigate(ButtonId button, bool *out_confirmed,
                             bool *should_exit)
{
    if (!out_confirmed || !should_exit) return;
    *out_confirmed = false;
    *should_exit = false;

    if (s_state == DELETE_CONFIRM_STATE_RESULT) {
        /* Any button dismisses the result screen. */
        *should_exit = true;
        return;
    }

    switch (button) {
    case BUTTON_LEFT:
        *should_exit = true;
        break;
    case BUTTON_RIGHT:
        if (s_item_count > 0) {
            *out_confirmed = true;
        }
        break;
    default:
        /* CENTER is intentionally a no-op here — it's the menu's own
         * select button, so a fast double-press can't chain into a
         * delete confirmation. */
        break;
    }
}

void delete_confirm_show_result(int deleted_count)
{
    s_state = DELETE_CONFIRM_STATE_RESULT;
    s_deleted_count = deleted_count;
}
