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

typedef struct queue_index_s queue_index_t;

/* ── Screen states ───────────────────────────────────────────────────── */

typedef enum {
    UI_SCREEN_HOME = 0,       /**< Home screen (face + status bar)       */
    UI_SCREEN_RECORDING,      /**< Recording screen (face + timer)       */
    UI_SCREEN_SAVED,          /**< Brief "Saved" display after stop      */
    UI_SCREEN_MENU,           /**< Main menu (7 items)                   */
    UI_SCREEN_FACE_SUBMENU,   /**< Face theme picker submenu             */
    UI_SCREEN_RECORDINGS_LIST,/**< Paged list of recorded WAV files      */
    UI_SCREEN_UNSENT_LIST,    /**< Paged list of unsent queue entries    */
    UI_SCREEN_INFO,           /**< Wi-Fi/SD/battery info                 */
    UI_SCREEN_DELETE_CONFIRM, /**< "Delete N recordings?" prompt         */
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
    UI_ACTION_SHOW_RECORDINGS,    /**< Navigate to Recordings list screen  */
    UI_ACTION_SHOW_UNSENT,        /**< Navigate to Unsent list screen      */
    UI_ACTION_UNSENT_SEND_ALL,    /**< Send All from Unsent screen         */
} ui_action_t;

/* ── Status info passed to screen renderers ──────────────────────────── */

typedef struct {
    bool     wifi_connected;       /**< Wi-Fi is associated              */
    bool     sd_mounted;           /**< SD card is present and mounted   */
    int      pending_uploads;      /**< Queue entries in pending/failed state */
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
 * @brief Report whether the SD card is mounted, for the home screen's
 *        status bar. Wi-Fi state updates itself via RECORDER_EVENTS
 *        (RECORDER_EVENT_WIFI_CONNECTED/DISCONNECTED); SD mount state
 *        has no such event, so app_main calls this once after mounting.
 *
 * @param mounted  true if the SD card is mounted and usable.
 */
void ui_task_set_sd_mounted(bool mounted);

/**
 * @brief Give the UI task a handle to the upload queue store, so the
 *        "Delete Sent" menu item can count and delete SENT recordings.
 *
 * @param queue  Queue handle from queue_store_init() (owned by app_main).
 */
void ui_task_set_queue_store(queue_index_t *queue);

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

/** Draw the Recordings list screen (paged WAV file browser). */
void recordings_list_screen_draw(void);

/** Handle a button press on the Recordings list screen. */
void recordings_list_navigate(ButtonId button, bool *should_exit);

/** Draw the Unsent list screen (paged queue browser, stubbed until Task 15). */
void unsent_list_screen_draw(void);

/** Handle a button press on the Unsent list screen. */
void unsent_list_navigate(ButtonId button, bool *should_exit);

/** Draw the Info screen (Wi-Fi status/SSID/IP, SD status, battery %). */
void info_screen_draw(const ui_status_t *status);

/** Which action the confirm screen (below) is guarding. */
typedef enum {
    DELETE_CONFIRM_SENT = 0, /**< Delete Sent: only SENT queue entries    */
    DELETE_CONFIRM_ALL,      /**< Delete All: every .wav on the SD card   */
    DELETE_CONFIRM_SHUTDOWN, /**< Shutdown: power off the board           */
} delete_confirm_kind_t;

/**
 * @brief Called when entering the confirm screen.
 *
 * @param kind         Which menu item triggered this (changes title/copy).
 * @param item_count   Number of recordings that would be deleted, or 1 for
 *                      DELETE_CONFIRM_SHUTDOWN (just needs to be > 0 so
 *                      RIGHT is armed).
 */
void delete_confirm_enter(delete_confirm_kind_t kind, int item_count);

/** Draw the delete confirm screen. */
void delete_confirm_screen_draw(void);

/**
 * @brief Handle a button press on the delete confirm screen.
 *
 * RIGHT   -> *out_confirmed = true, caller should perform the delete then
 *            call delete_confirm_show_result(). Deliberately NOT the same
 *            button (CENTER) used to select this menu item, so a fast
 *            double-press can't chain straight into a delete.
 * LEFT    -> *should_exit = true (cancel, back to menu).
 * CENTER  -> no-op while asking.
 * Any button while the result is showing -> *should_exit = true.
 */
void delete_confirm_navigate(ButtonId button, bool *out_confirmed,
                             bool *should_exit);

/** Switch the confirm screen into its "Deleted N files" result state. */
void delete_confirm_show_result(int deleted_count);

#ifdef __cplusplus
}
#endif
