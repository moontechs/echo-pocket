/** @file ui_screen_logic.c
 * @brief Pure screen state-machine logic — unit-testable without hardware.
 *
 * This is separated from ui_task.c so logic_tests can link it directly
 * without pulling in FreeRTOS tasks, display, or event loops.
 *
 * Note: the main menu (UI_SCREEN_MENU) and Face submenu
 * (UI_SCREEN_FACE_SUBMENU) have stateful cursor-based navigation that
 * requires knowing the cursor position to determine CENTER-button outcomes.
 * The pure screen transitions here only handle the stateless parts
 * (LEFT/RIGHT for screen entry/exit); menu cursor navigation is handled
 * directly in ui_task via menu_navigate() / face_submenu_navigate() from
 * menu.h.
 */

#include "ui_task.h"

const char *ui_screen_name(ui_screen_t screen)
{
    switch (screen) {
    case UI_SCREEN_HOME:          return "Home";
    case UI_SCREEN_RECORDING:     return "Recording";
    case UI_SCREEN_SAVED:         return "Saved";
    case UI_SCREEN_MENU:          return "Menu";
    case UI_SCREEN_FACE_SUBMENU:  return "Face";
    case UI_SCREEN_RECORDINGS_LIST: return "RecordingsList";
    case UI_SCREEN_UNSENT_LIST:   return "UnsentList";
    case UI_SCREEN_INFO:          return "Info";
    case UI_SCREEN_DELETE_CONFIRM: return "DeleteConfirm";
    default:                      return "Unknown";
    }
}

ui_screen_t ui_screen_next(ui_screen_t current, ButtonId button,
                           ui_action_t *out_action)
{
    if (!out_action) return current;

    *out_action = UI_ACTION_NONE;

    switch (current) {
    case UI_SCREEN_HOME:
        if (button == BUTTON_CENTER) {
            *out_action = UI_ACTION_START_RECORDING;
            return UI_SCREEN_RECORDING;
        }
        if (button == BUTTON_RIGHT) {
            *out_action = UI_ACTION_ENTER_MENU;
            return UI_SCREEN_MENU;
        }
        /* Left: no-op in home */
        break;

    case UI_SCREEN_RECORDING:
        if (button == BUTTON_CENTER) {
            *out_action = UI_ACTION_STOP_RECORDING;
            return UI_SCREEN_SAVED;
        }
        /* Left/Right ignored during recording */
        break;

    case UI_SCREEN_SAVED:
        /* Any button press dismisses the "Saved" screen back to home. */
        *out_action = UI_ACTION_NONE;
        return UI_SCREEN_HOME;

    case UI_SCREEN_MENU:
        if (button == BUTTON_LEFT) {
            /* Back to home from menu */
            return UI_SCREEN_HOME;
        }
        /* RIGHT and CENTER are handled by ui_task via menu_navigate()
         * — stay in MENU, action determined by menu state machine */
        break;

    case UI_SCREEN_FACE_SUBMENU:
        if (button == BUTTON_LEFT) {
            /* Back to main menu from face submenu */
            return UI_SCREEN_MENU;
        }
        /* RIGHT and CENTER handled by ui_task via face_submenu_navigate() */
        break;

    case UI_SCREEN_RECORDINGS_LIST:
        if (button == BUTTON_LEFT) {
            /* Back to main menu */
            return UI_SCREEN_MENU;
        }
        /* RIGHT handled by ui_task via recordings_list_navigate() */
        break;

    case UI_SCREEN_UNSENT_LIST:
        if (button == BUTTON_LEFT) {
            /* Back to main menu */
            return UI_SCREEN_MENU;
        }
        /* RIGHT and CENTER handled by ui_task via unsent_list_navigate() */
        break;

    case UI_SCREEN_INFO:
        if (button == BUTTON_LEFT) {
            /* Back to main menu */
            return UI_SCREEN_MENU;
        }
        /* RIGHT/CENTER: no-op — status only, nothing to select */
        break;

    case UI_SCREEN_DELETE_CONFIRM:
        /* All button handling (confirm/cancel/dismiss result) goes
         * through ui_task via delete_confirm_navigate() — stay put,
         * ui_task drives the screen transition once the entry exits. */
        break;
    }

    return current;
}
