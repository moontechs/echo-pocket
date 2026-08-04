/** @file ui_task.c
 * @brief UI task — sole ButtonEvent consumer, face-theme driver,
 *        screen manager, RECORDER_EVENTS subscriber.
 *
 * This task is the only place that reads from the ButtonEvent queue
 * (buttons.c → ui_task → recorder_start/stop).  It also subscribes to
 * RECORDER_EVENTS and maps them to the active face theme's setEvent().
 *
 * FreeRTOS priority: NORMAL (below audio capture/AFE/writer).
 * Stack size: generous to accommodate screen rendering + face theme
 *             stack frames; measured peak ~2800 bytes on reference.
 */

#include "ui_task.h"
#include "display.h"
#include "board.h"
#include "device_events.h"
#include "face_registry.h"
#include "recorder.h"
#include "audio_process.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "face_plugin.hpp"

static const char *TAG = "ui_task";

/* ── Task constants ──────────────────────────────────────────────────── */

/** Task stack size in bytes. */
#define UI_TASK_STACK_SIZE        4096

/** Task priority — NORMAL, below audio tasks. */
#define UI_TASK_PRIORITY          (configMAX_PRIORITIES - 4)

/** How often the UI updates (ms). ~10 fps gives responsive face animation
 *  without competing with audio capture for CPU time. */
#define UI_UPDATE_PERIOD_MS       100

/** How long the "Saved" screen stays before returning to Home (ms). */
#define SAVED_SCREEN_DURATION_MS  2000

/** Button event queue depth (same as what buttons_init expects). */
#define UI_BUTTON_QUEUE_DEPTH     4

/* ── Internal state ──────────────────────────────────────────────────── */

static QueueHandle_t  s_button_queue  = NULL;
static TaskHandle_t   s_task          = NULL;
static volatile bool  s_running       = false;

static ui_screen_t    s_screen        = UI_SCREEN_HOME;
static uint32_t       s_rec_start_ms  = 0;
static uint32_t       s_saved_enter_ms = 0;

static bool           s_wifi_connected = false;
static bool           s_sd_mounted     = false;
static int            s_pending_uploads = 0;
static bool           s_battery_present = false;
static int            s_battery_percent = -1;
static bool           s_charging        = false;

/* ── RECORDER_EVENTS handler ─────────────────────────────────────────── */

static void ui_event_handler(void *handler_arg, esp_event_base_t base,
                             int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)event_data;

    if (base != RECORDER_EVENTS) return;

    FacePlugin *face = face_registry_get_active();
    if (!face) return;

    switch ((recorder_event_id_t)id) {
    case RECORDER_EVENT_STARTED:
        face->setEvent(FaceEvent::Recording);
        break;

    case RECORDER_EVENT_STOPPED:
        face->setEvent(FaceEvent::Saving);
        break;

    case RECORDER_EVENT_SAVED:
        /* Transition to Saved screen in the main loop */
        s_screen = UI_SCREEN_SAVED;
        s_saved_enter_ms = (uint32_t)(esp_timer_get_time() / 1000);
        break;

    case RECORDER_EVENT_UPLOAD_STARTED:
        face->setEvent(FaceEvent::Uploading);
        break;

    case RECORDER_EVENT_UPLOAD_SUCCESS:
        face->setEvent(FaceEvent::UploadSuccess);
        break;

    case RECORDER_EVENT_UPLOAD_ERROR:
        face->setEvent(FaceEvent::UploadError);
        break;

    case RECORDER_EVENT_BATTERY_WARNING:
    case RECORDER_EVENT_BATTERY_CRITICAL:
        face->setEvent(FaceEvent::LowBattery);
        break;

    default:
        break;
    }
}

/* ── Build ui_status_t from current state ────────────────────────────── */

static void build_status(ui_status_t *status)
{
    status->wifi_connected  = s_wifi_connected;
    status->sd_mounted      = s_sd_mounted;
    status->pending_uploads = s_pending_uploads;
    status->battery_present = s_battery_present;
    status->battery_percent = s_battery_percent;
    status->charging        = s_charging;
    status->show_saved      = (s_screen == UI_SCREEN_SAVED);

    if (s_screen == UI_SCREEN_RECORDING || s_screen == UI_SCREEN_SAVED) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms >= s_rec_start_ms) {
            status->recording_elapsed_ms = now_ms - s_rec_start_ms;
        } else {
            status->recording_elapsed_ms = 0;
        }
    } else {
        status->recording_elapsed_ms = 0;
    }
}

/* ── Main task loop ──────────────────────────────────────────────────── */

static void ui_task_loop(void *arg)
{
    (void)arg;

    uint32_t last_update_ms = 0;
    ButtonEvent btn_evt;
    ui_status_t status;

    ESP_LOGI(TAG, "UI task started (prio %d, stack %d)",
             (int)UI_TASK_PRIORITY, (int)UI_TASK_STACK_SIZE);

    while (s_running) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        bool screen_changed = false;

        /* ── Process button events ────────────────────────────── */
        while (xQueueReceive(s_button_queue, &btn_evt, 0) == pdTRUE) {
            ui_action_t action;
            ui_screen_t next = ui_screen_next(s_screen, btn_evt.button, &action);

            if (next != s_screen) {
                screen_changed = true;
            }
            s_screen = next;

            switch (action) {
            case UI_ACTION_START_RECORDING:
                s_rec_start_ms = now_ms;
                recorder_start();
                ESP_LOGI(TAG, "UI: start recording");
                break;

            case UI_ACTION_STOP_RECORDING:
                recorder_stop();
                ESP_LOGI(TAG, "UI: stop recording");
                break;

            case UI_ACTION_NONE:
            default:
                break;
            }

            /* After handling one button, break to render */
            if (screen_changed) break;
        }

        /* ── Saved screen auto-dismiss ──────────────────────── */
        if (s_screen == UI_SCREEN_SAVED) {
            uint32_t elapsed = now_ms - s_saved_enter_ms;
            if (elapsed >= SAVED_SCREEN_DURATION_MS) {
                FacePlugin *face = face_registry_get_active();
                if (face) {
                    face->setEvent(FaceEvent::Idle);
                }
                s_screen = UI_SCREEN_HOME;
                screen_changed = true;
            }
        }

        /* ── Render frame (rate-limited) ────────────────────── */
        if (face_registry_should_update(now_ms) || screen_changed) {
            /* Update face animation state */
            FacePlugin *face = face_registry_get_active();
            if (face) {
                uint32_t delta = (last_update_ms > 0 && now_ms > last_update_ms)
                                 ? now_ms - last_update_ms : UI_UPDATE_PERIOD_MS;
                if (delta > 500) delta = 500; /* clamp missed updates */

                float voice_level = audio_process_get_voice_level();
                bool voice_active = audio_process_is_voice_active();
                face->update(voice_level, voice_active, delta);
            }

            /* Build status struct and render the current screen */
            build_status(&status);

            switch (s_screen) {
            case UI_SCREEN_HOME:
                home_screen_draw(&status);
                break;

            case UI_SCREEN_RECORDING:
            case UI_SCREEN_SAVED:
                recording_screen_draw(&status);
                break;
            }

            last_update_ms = now_ms;
        }

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(TAG, "UI task stopped");
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void ui_task_init(QueueHandle_t button_queue)
{
    if (!button_queue) {
        ESP_LOGE(TAG, "ui_task_init: button_queue is NULL");
        return;
    }

    s_button_queue = button_queue;
    s_screen       = UI_SCREEN_HOME;
    s_running      = true;

    /* Subscribe to RECORDER_EVENTS */
    esp_event_handler_instance_register(RECORDER_EVENTS,
                                        ESP_EVENT_ANY_ID,
                                        ui_event_handler,
                                        NULL,
                                        NULL);

    /* Give the active face theme its initial Idle event */
    FacePlugin *face = face_registry_get_active();
    if (face) {
        face->setEvent(FaceEvent::Idle);
    }

    BaseType_t created = xTaskCreate(
        ui_task_loop,
        "ui_task",
        UI_TASK_STACK_SIZE,
        NULL,
        UI_TASK_PRIORITY,
        &s_task
    );

    if (created != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Failed to create UI task");
        return;
    }

    ESP_LOGI(TAG, "UI initialized (screen=%s)", ui_screen_name(s_screen));
}

void ui_task_deinit(void)
{
    s_running = false;

    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_task = NULL;
    }

    esp_event_handler_instance_unregister(RECORDER_EVENTS,
                                          ESP_EVENT_ANY_ID,
                                          NULL);

    s_button_queue = NULL;
}

void ui_task_set_recording_start(uint32_t start_ms)
{
    s_rec_start_ms = start_ms;
}

void ui_task_set_pending_uploads(int count)
{
    s_pending_uploads = (count < 0) ? 0 : count;
}
