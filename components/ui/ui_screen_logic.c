/** @file ui_screen_logic.c
 * @brief Pure screen state-machine logic — unit-testable without hardware.
 *
 * This is separated from ui_task.c so logic_tests can link it directly
 * without pulling in FreeRTOS tasks, display, or event loops.
 */

#include "ui_task.h"

const char *ui_screen_name(ui_screen_t screen)
{
    switch (screen) {
    case UI_SCREEN_HOME:      return "Home";
    case UI_SCREEN_RECORDING: return "Recording";
    case UI_SCREEN_SAVED:     return "Saved";
    default:                  return "Unknown";
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
        /* Left: no-op in home (menu will be Task 12).        */
        /* Right: no-op in home (menu will be Task 12).       */
        break;

    case UI_SCREEN_RECORDING:
        if (button == BUTTON_CENTER) {
            *out_action = UI_ACTION_STOP_RECORDING;
            return UI_SCREEN_SAVED;
        }
        break;

    case UI_SCREEN_SAVED:
        /* Any button press dismisses the "Saved" screen back to home. */
        *out_action = UI_ACTION_NONE;
        return UI_SCREEN_HOME;
    }

    return current;
}
