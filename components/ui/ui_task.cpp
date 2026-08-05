/** @file ui_task.c
 * @brief UI task — sole ButtonEvent consumer, face-theme driver,
 *        screen manager, RECORDER_EVENTS subscriber.
 *
 * This task is the only place that reads from the ButtonEvent queue
 * (buttons.c → ui_task → recorder_start/stop).  It also subscribes to
 * RECORDER_EVENTS and maps them to the active face theme's setEvent().
 *
 * Includes the main menu (Task 12): LEFT=back, CENTER=select,
 * RIGHT=next — with Face submenu for live theme switching and
 * config persistence via config_save().
 *
 * FreeRTOS priority: NORMAL (below audio capture/AFE/writer).
 * Stack size: generous to accommodate screen rendering + face theme
 *             stack frames; measured peak ~2800 bytes on reference.
 */

#include "ui_task.h"
#include "menu.h"
#include "list_screens.h"
#include "display.h"
#include "board.h"
#include "device_events.h"
#include "face_registry.h"
#include "recorder.h"
#include "audio_process.h"
#include "config.h"
#include "battery.h"

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

/** Path to config file on SD for theme persistence. */
#define CONFIG_FILE_PATH          "/sdcard/echo-pocket/config/recorder.ini"

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

/* Menu / Face submenu state (Task 12) */
static menu_state_t         s_menu_state;
static face_submenu_state_t s_face_submenu_state;

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
    status->battery_present = battery_is_present();
    status->battery_percent = battery_percent();
    status->charging        = battery_is_charging();
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

/* ── Theme persistence helper ────────────────────────────────────────── */

/**
 * Switch to the theme at @p theme_index in the registry and persist the
 * new theme id to recorder.ini via config_save().
 */
static void apply_and_persist_theme(int theme_index)
{
    FacePlugin *plugin = face_registry_get_by_index(theme_index);
    if (!plugin) {
        ESP_LOGE(TAG, "No theme at index %d", theme_index);
        return;
    }

    const char *theme_id = plugin->id();
    ESP_LOGI(TAG, "Switching to theme '%s'", theme_id);

    /* Live switch — no reboot needed */
    face_registry_begin(theme_id);

    /* Persist to config */
    RecorderConfig cfg = config_load(CONFIG_FILE_PATH, NULL);
    strncpy(cfg.theme, theme_id, CONFIG_MAX_THEME_NAME - 1);
    cfg.theme[CONFIG_MAX_THEME_NAME - 1] = '\0';
    config_err_t err = config_save(&cfg, CONFIG_FILE_PATH);

    if (err == CONFIG_OK) {
        ESP_LOGI(TAG, "Theme '%s' persisted to config", theme_id);
    } else {
        ESP_LOGW(TAG, "Failed to persist theme: %s", config_err_str(err));
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

    /* Initialise menu state — cursor at first item */
    menu_state_init(&s_menu_state);

    while (s_running) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        bool screen_changed = false;

        /* ── Process button events ────────────────────────────── */
        while (xQueueReceive(s_button_queue, &btn_evt, 0) == pdTRUE) {

            /* ── Menu screen: use menu_navigate() ─────────────── */
            if (s_screen == UI_SCREEN_MENU) {
                menu_action_t menu_action;
                menu_navigate(&s_menu_state, btn_evt.button, &menu_action);

                switch (menu_action) {
                case MENU_ACTION_START_RECORDING:
                    s_rec_start_ms = now_ms;
                    recorder_start();
                    s_screen = UI_SCREEN_RECORDING;
                    screen_changed = true;
                    break;

                case MENU_ACTION_ENTER_FACE_SUBMENU:
                    face_submenu_state_init(&s_face_submenu_state,
                                            face_registry_count());
                    s_screen = UI_SCREEN_FACE_SUBMENU;
                    screen_changed = true;
                    break;

                case MENU_ACTION_SEND_ALL:
                    /* Task 17 will wire this — currently a stub */
                    ESP_LOGI(TAG, "Send All (stub — Task 17)");
                    break;

                case MENU_ACTION_SHOW_STUB:
                    /* Route specific stub items to their
                     * real screens if available. */
                    if (s_menu_state.cursor == MENU_ITEM_RECORDINGS) {
                        s_screen = UI_SCREEN_RECORDINGS_LIST;
                        recordings_list_enter();
                        screen_changed = true;
                    } else if (s_menu_state.cursor == MENU_ITEM_UNSENT) {
                        s_screen = UI_SCREEN_UNSENT_LIST;
                        unsent_list_enter();
                        screen_changed = true;
                    } else {
                        /* Wi-Fi, Telegram, Settings, Info — stubs */
                        ESP_LOGI(TAG, "Menu item '%s' selected (stub)",
                                 menu_item_label(s_menu_state.cursor));
                    }
                    break;

                case MENU_ACTION_BACK_TO_HOME:
                    s_screen = UI_SCREEN_HOME;
                    screen_changed = true;
                    break;

                case MENU_ACTION_NONE:
                default:
                    break;
                }

                if (screen_changed) break;
                continue;
            }

            /* ── Face submenu: use face_submenu_navigate() ───── */
            if (s_screen == UI_SCREEN_FACE_SUBMENU) {
                int theme_index = -1;
                bool should_exit = false;
                face_submenu_navigate(&s_face_submenu_state,
                                      btn_evt.button,
                                      &theme_index, &should_exit);

                if (should_exit) {
                    s_screen = UI_SCREEN_MENU;
                    screen_changed = true;
                } else if (theme_index >= 0) {
                    /* User selected a theme — switch and persist */
                    apply_and_persist_theme(theme_index);
                    s_screen = UI_SCREEN_MENU;
                    screen_changed = true;
                }

                if (screen_changed) break;
                continue;
            }

            /* ── Recordings list screen ───────────────────────── */
            if (s_screen == UI_SCREEN_RECORDINGS_LIST) {
                bool should_exit = false;
                recordings_list_navigate(btn_evt.button, &should_exit);
                if (should_exit) {
                    s_screen = UI_SCREEN_MENU;
                    screen_changed = true;
                } else {
                    /* Cursor moved — re-render */
                    screen_changed = true;
                }
                if (screen_changed) break;
                continue;
            }

            /* ── Unsent list screen ───────────────────────────── */
            if (s_screen == UI_SCREEN_UNSENT_LIST) {
                bool should_exit = false;
                unsent_list_navigate(btn_evt.button, &should_exit);
                if (should_exit) {
                    s_screen = UI_SCREEN_MENU;
                    screen_changed = true;
                } else if (btn_evt.button == BUTTON_CENTER) {
                    /* CENTER on Unsent = Send All (Task 17 wires this) */
                    ESP_LOGI(TAG, "Send All from Unsent screen (stub — Task 17)");
                } else {
                    /* Cursor moved — re-render */
                    screen_changed = true;
                }
                if (screen_changed) break;
                continue;
            }

            /* ── Standard screens (Home/Recording/Saved) ──────── */
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

            case UI_ACTION_ENTER_MENU:
                /* Reset menu cursor to top when entering menu */
                menu_state_init(&s_menu_state);
                ESP_LOGI(TAG, "UI: entering main menu");
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
        bool menu_always_render = (s_screen == UI_SCREEN_MENU ||
                                   s_screen == UI_SCREEN_FACE_SUBMENU);
        bool should_render = face_registry_should_update(now_ms)
                             || screen_changed || menu_always_render;

        if (should_render) {
            /* Update face animation state (not needed for menus
             * but kept for Home/Recording fallthrough) */
            if (s_screen == UI_SCREEN_HOME ||
                s_screen == UI_SCREEN_RECORDING ||
                s_screen == UI_SCREEN_SAVED) {
                FacePlugin *face = face_registry_get_active();
                if (face) {
                    uint32_t delta = (last_update_ms > 0 && now_ms > last_update_ms)
                                     ? now_ms - last_update_ms : UI_UPDATE_PERIOD_MS;
                    if (delta > 500) delta = 500; /* clamp missed updates */

                    float voice_level = audio_process_get_voice_level();
                    bool voice_active = audio_process_is_voice_active();
                    face->update(voice_level, voice_active, delta);
                }
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

            case UI_SCREEN_MENU:
                menu_screen_draw(&s_menu_state);
                break;

            case UI_SCREEN_FACE_SUBMENU:
                face_submenu_screen_draw(&s_face_submenu_state);
                break;

            case UI_SCREEN_RECORDINGS_LIST:
                recordings_list_screen_draw();
                break;

            case UI_SCREEN_UNSENT_LIST:
                unsent_list_screen_draw();
                break;
            }

            display_flush();
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
