/** @file ui_task.h
 * @brief UI task — sole ButtonEvent consumer, drives face themes,
 *        renders home and recording screens, subscribes to RECORDER_EVENTS.
 *
 * This is the main UI orchestrator.  It reads button events and
 * device-state events, routes them to the active face theme, and
 * renders the correct screen to the display.
 *
 * FreeRTOS priority: NORMAL (below audio capture/AFE/writer).
 */

#pragma once

#include "buttons.h"
#include "menu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen states ───────────────────────────────────────────────────── */

typedef enum {
    UI_SCREEN_HOME = 0,       /**< Home screen (face + status bar)       */
    UI_SCREEN_RECORDING,      /**< Recording screen (face + timer)       */
    UI_SCREEN_SAVED,          /**< Brief "Saved" display after stop      */
    UI_SCREEN_MENU,           /**< Main menu (9 items)                   */
    UI_SCREEN_FACE_SUBMENU,   /**< Face theme picker submenu             */
} ui_screen_t;

/* ── Actions the screen state machine can request ────────────────────── */

typedef enum {
    UI_ACTION_NONE = 0,           /**< No side-effect required            */
    UI_ACTION_START_RECORDING,    /**< Call recorder_start()              */
    UI_ACTION_STOP_RECORDING,     /**< Call recorder_stop()               */
    UI_ACTION_ENTER_MENU,         /**< Navigate to the main menu          */
    UI_ACTION_ENTER_FACE_SUBMENU, /**< Navigate to face theme picker      */
    UI_ACTION_SEND_ALL,           /**< Trigger manual upload drain        */
    UI_ACTION_STUB,               /**< Show stub screen (Wi-Fi/etc.)      */
    UI_ACTION_SELECT_THEME,       /**< Select a face theme (index in arg) */
} ui_action_t;

/* ── Status info passed to screen renderers ──────────────────────────── */

typedef struct {
    bool     wifi_connected;       /**< Wi-Fi is associated              */
    bool     sd_mounted;           /**< SD card is present and mounted   */
    int      pending_uploads;      /**< Queue entries in pending state   */
    bool     battery_present;      /**< Battery monitoring available     */
    int      battery_percent;      /**< 0–100, or -1 if unknown         */
    bool     charging;             /**< USB/charger connected            */
    uint32_t recording_elapsed_ms; /**< Milliseconds since rec start     */
    bool     show_saved;           /**< Overlay "Saved" text             */
} ui_status_t;

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialize the UI task.
 *
 * Becomes the sole consumer of @p button_queue and the sole subscriber
 * to RECORDER_EVENTS for face-theme dispatch.
 *
 * @param button_queue  FreeRTOS queue carrying ButtonEvent structs
 *                      (created by the caller, fed by buttons_init).
 */
void ui_task_init(QueueHandle_t button_queue);

/**
 * @brief De-initialize the UI task and clean up resources.
 */
void ui_task_deinit(void);

/**
 * @brief Set the recording start time so the UI can display elapsed time.
 *
 * Called by the RECORDER_EVENT_STARTED handler.
 *
 * @param start_ms  Monotonic timestamp when recording began.
 */
void ui_task_set_recording_start(uint32_t start_ms);

/**
 * @brief Update the pending upload count shown on the home screen.
 *
 * Called by the upload task (Task 17) or queue_store (Task 15).
 *
 * @param count  Number of pending uploads.
 */
void ui_task_set_pending_uploads(int count);

/* ── Pure screen state machine (unit-testable) ───────────────────────── */

/**
 * @brief Determine the next screen and required action from a button press.
 *
 * Pure function — no global state, no FreeRTOS, no display.  Unit-testable
 * under logic_tests without hardware.
 *
 * @param current     Current screen state.
 * @param button      Button that was pressed.
 * @param[out] out_action  Required side-effect action.
 * @return            New screen state.
 */
ui_screen_t ui_screen_next(ui_screen_t current, ButtonId button,
                           ui_action_t *out_action);

/**
 * @brief Return a human-readable name for a screen state (debug/logging).
 */
const char *ui_screen_name(ui_screen_t screen);

/* ── Screen renderers (called by ui_task only) ───────────────────────── */

/** Draw the home screen: face + status bar overlay. */
void home_screen_draw(const ui_status_t *status);

/** Draw the recording screen: face + REC indicator + timer overlay. */
void recording_screen_draw(const ui_status_t *status);

#ifdef __cplusplus
}
#endif
