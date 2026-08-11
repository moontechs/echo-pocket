/** @file upload_task.c
 * @brief Upload task — drains the queue to Telegram.
 *
 * The upload_task waits for triggers (Wi-Fi connected or manual Send All),
 * then drains pending queue entries one at a time via the Telegram client.
 *
 * Route of a single entry through the drain loop:
 *   1. Mark QUEUE_STATE_UPLOADING → persist
 *   2. Convert the WAV to a temp MP3 via wav_to_mp3()
 *   3. Send via telegram_client_send_audio_to_channels() (no caption)
 *   4. Classify result → upload_drain_compute_outcome()
 *   5. Apply outcome (mark sent/failed/pending, delete file if applicable)
 *   6. Post RECORDER_EVENT_UPLOAD_SUCCESS / UPLOAD_ERROR
 *
 * Core logic (upload_drain_compute_outcome) is a pure function tested
 * under logic_tests.  The task itself is hardware-dependent (needs
 * FreeRTOS, esp_event, real SD for file deletion).
 */

#include "upload_task.h"
#include "telegram_client.h"
#include "wav_to_mp3.h"
#include "recorder.h"
#include "device_events.h"
#include "battery.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_timer.h"

static const char *TAG = "upload_task";

/* ── Trigger types ───────────────────────────────────────────────────── */

typedef enum {
    TRIGGER_WIFI_CONNECTED = 0,  /**< Auto-upload: drain if auto_upload=true   */
    TRIGGER_SEND_ALL,            /**< Manual: drain regardless of auto_upload   */
} upload_trigger_t;

/* ── Task-internal state ─────────────────────────────────────────────── */

typedef struct {
    const RecorderConfig  *cfg;      /**< Live config pointer (statically allocated) */
    queue_index_t         *queue;    /**< Upload queue                          */
    QueueHandle_t          cmd_queue; /**< FreeRTOS queue of upload_trigger_t   */
    volatile bool          drain_active; /**< True while the drain loop runs   */
    volatile bool          send_all_pending; /**< True if Send All was triggered */
} upload_state_t;

static upload_state_t s_state;

/* Task stack lives in PSRAM (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY):
 * internal RAM is the scarce resource on this board (single-digit KB free
 * at boot — see AGENTS.md), and this task's HTTPS/multipart upload needs
 * more stack than that budget affords via a normal xTaskCreate. */
static StaticTask_t s_upload_task_tcb;
static StackType_t *s_upload_task_stack = NULL;

/* ── Pure logic ──────────────────────────────────────────────────────── */

#if 0 /* Implemented in upload_logic.c so the state machine is host-testable. */
upload_drain_outcome_t upload_drain_compute_outcome(
    upload_send_result_t result,
    int current_attempts,
    int max_attempts,
    bool delete_after_upload)
{
    upload_drain_outcome_t out;
    int next_attempts = current_attempts + 1;

    switch (result) {

    case UPLOAD_SEND_OK:
        out.new_state          = QUEUE_STATE_SENT;
        out.new_attempts       = next_attempts;
        out.should_delete_file = delete_after_upload;
        break;

    case UPLOAD_SEND_FAIL_RETRYABLE:
        if (next_attempts >= max_attempts) {
            /* Cap reached — terminal */
            out.new_state          = QUEUE_STATE_FAILED;
            out.new_attempts       = next_attempts;
            out.should_delete_file = false;
        } else {
            /* Revert to pending for a future retry */
            out.new_state          = QUEUE_STATE_PENDING;
            out.new_attempts       = next_attempts;
            out.should_delete_file = false;
        }
        break;

    case UPLOAD_SEND_FAIL_FATAL:
        /* Hard failure — mark failed now regardless of attempts */
        out.new_state          = QUEUE_STATE_FAILED;
        out.new_attempts       = next_attempts;
        out.should_delete_file = false;
        break;

    default:
        /* Safety: treat unknown as retryable */
        out.new_state          = QUEUE_STATE_PENDING;
        out.new_attempts       = next_attempts;
        out.should_delete_file = false;
        break;
    }

    return out;
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

/**
 * Classify a telegram_err_t into an upload_send_result_t.
 */
#endif

static upload_send_result_t classify_send_error(telegram_err_t err)
{
    switch (err) {
    case TELEGRAM_OK:
        return UPLOAD_SEND_OK;

    /* Hard failures — won't resolve with retries */
    case TELEGRAM_ERR_FILE_NOT_FOUND:
    case TELEGRAM_ERR_FILE_TOO_LARGE:
    case TELEGRAM_ERR_NULL_PARAM:
    case TELEGRAM_ERR_OOM:
        return UPLOAD_SEND_FAIL_FATAL;

    /* Transient failures — worth retrying */
    case TELEGRAM_ERR_CONNECT:
    case TELEGRAM_ERR_HTTP:
    case TELEGRAM_ERR_API:
    case TELEGRAM_ERR_PARSE:
    case TELEGRAM_ERR_ABORTED:
    default:
        return UPLOAD_SEND_FAIL_RETRYABLE;
    }
}

/**
 * Build the temporary MP3 path used for one upload: the WAV path with its
 * extension replaced by ".mp3". Deleted again after each send attempt —
 * only the WAV is durable queue state.
 */
static void build_temp_mp3_path(const char *wav_path, char *out, size_t out_size)
{
    strncpy(out, wav_path, out_size - 1);
    out[out_size - 1] = '\0';

    size_t len = strlen(out);
    if (len >= 4 && strcmp(out + len - 4, ".wav") == 0) {
        strcpy(out + len - 4, ".mp3");
    } else {
        strncat(out, ".mp3", out_size - strlen(out) - 1);
    }
}

/**
 * Delete a WAV file from SD, if it exists.
 * Emits a warning on failure but never blocks the drain loop.
 */
static void upload_delete_wav(const char *file_path)
{
    if (!file_path || file_path[0] == '\0') return;

    struct stat st;
    if (stat(file_path, &st) != 0) {
        ESP_LOGW(TAG, "Cannot stat file for deletion: %s", file_path);
        return;
    }

    if (unlink(file_path) != 0) {
        ESP_LOGW(TAG, "Failed to delete WAV: %s", file_path);
        return;
    }

    ESP_LOGI(TAG, "Deleted WAV: %s", file_path);
}

/* ── Drain loop ──────────────────────────────────────────────────────── */

/**
 * Drain all pending entries, one at a time.
 *
 * Each iteration:
 *   1. Pause if the recorder is actively recording.
 *   2. Get the next pending entry (handles the queue being updated
 *      mid-drain — e.g. a new recording finishes while we drain).
 *   3. Mark uploading → send → classify → compute outcome → apply.
 *
 * @return Number of entries successfully sent.
 */
static int upload_drain_loop(void)
{
    int sent_count = 0;

    while (true) {
        /* ── Don't preempt an active recording ────────────────────── */
        if (recorder_is_recording()) {
            ESP_LOGI(TAG, "Drain paused — recording in progress");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* ── Get next pending entry ───────────────────────────────── */
        queue_entry_t *entry = queue_store_get_next_pending(s_state.queue);
        if (!entry) {
            /* Nothing left to do */
            break;
        }

        /* ── Mark uploading ───────────────────────────────────────── */
        queue_store_err_t qerr = queue_store_mark_uploading(
            s_state.queue, entry);
        if (qerr != QUEUE_STORE_OK) {
            ESP_LOGE(TAG, "Failed to mark %s uploading: %s — skipping",
                     entry->id, queue_store_err_str(qerr));
            /* Can't persist — leave the entry and stop drain */
            break;
        }

        /* ── Post event ───────────────────────────────────────────── */
        esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_UPLOAD_STARTED,
                       NULL, 0, pdMS_TO_TICKS(100));

        /* ── Check battery before auto-upload ──────────────────── */
        if (battery_should_block_upload(battery_percent(), entry->size)) {
            ESP_LOGW(TAG, "Skipping %s — battery too low for %" PRIu32 " bytes",
                     entry->id, entry->size);
            /* Revert to pending so it's retried when power returns. */
            queue_store_err_t qerr = queue_store_revert_to_pending(
                s_state.queue, entry, entry->attempts);
            if (qerr != QUEUE_STORE_OK) {
                ESP_LOGE(TAG, "Failed to revert %s: %s",
                         entry->id, queue_store_err_str(qerr));
            }
            /* The first pending entry remains selected, so continuing would
             * spin on it and repeatedly write the queue. */
            break;
        }

        /* ── Convert to MP3 and send (no caption — audio only) ────── */
        char mp3_path[288];
        build_temp_mp3_path(entry->file, mp3_path, sizeof(mp3_path));

        char upload_filename[64];
        telegram_format_audio_filename(entry->id, time(NULL),
                                       (uint32_t)(esp_timer_get_time() / 1000000),
                                       upload_filename, sizeof(upload_filename));

        int message_id = 0;
        telegram_err_t terr;
        if (wav_to_mp3(entry->file, mp3_path)) {
            terr = telegram_client_send_audio_to_channels(
                s_state.cfg, mp3_path, upload_filename, &message_id);
            remove(mp3_path);
        } else {
            ESP_LOGE(TAG, "Failed to convert %s to MP3", entry->id);
            terr = TELEGRAM_ERR_FILE_NOT_FOUND;
        }

        /* ── Classify and compute outcome ─────────────────────────── */
        upload_send_result_t sres = classify_send_error(terr);
        upload_drain_outcome_t outcome = upload_drain_compute_outcome(
            sres, entry->attempts, UPLOAD_MAX_ATTEMPTS,
            s_state.cfg->delete_after_upload);

        /* ── Apply outcome ────────────────────────────────────────── */
        switch (outcome.new_state) {

        case QUEUE_STATE_SENT: {
            qerr = queue_store_mark_sent(s_state.queue, entry, message_id);
            if (qerr != QUEUE_STORE_OK) {
                ESP_LOGE(TAG, "Failed to mark %s sent: %s",
                         entry->id, queue_store_err_str(qerr));
                /* State not persisted — entry stuck as uploading.
                 * Next boot's crash recovery will revert to pending. */
            } else {
                sent_count++;
                esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_UPLOAD_SUCCESS,
                               NULL, 0, pdMS_TO_TICKS(100));

                /* Delete WAV file AFTER queue state is durably "sent" */
                if (outcome.should_delete_file) {
                    upload_delete_wav(entry->file);
                }
            }
            break;
        }

        case QUEUE_STATE_PENDING: {
            /* Retryable failure — revert to pending with incremented attempts */
            qerr = queue_store_revert_to_pending(s_state.queue, entry,
                                                  outcome.new_attempts);
            if (qerr != QUEUE_STORE_OK) {
                ESP_LOGE(TAG, "Failed to revert %s to pending: %s",
                         entry->id, queue_store_err_str(qerr));
            }
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_UPLOAD_ERROR,
                           NULL, 0, pdMS_TO_TICKS(100));
            break;
        }

        case QUEUE_STATE_FAILED: {
            qerr = queue_store_mark_failed(s_state.queue, entry);
            if (qerr != QUEUE_STORE_OK) {
                ESP_LOGE(TAG, "Failed to mark %s failed: %s",
                         entry->id, queue_store_err_str(qerr));
            }
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_UPLOAD_ERROR,
                           NULL, 0, pdMS_TO_TICKS(100));
            break;
        }

        default:
            /* QUEUE_STATE_RECORDING / QUEUE_STATE_UPLOADING —
             * shouldn't arrive here; treat as no-op */
            ESP_LOGW(TAG, "Unexpected outcome state %d for %s — skipping",
                     (int)outcome.new_state, entry->id);
            break;
        }

        /* Small yield between entries to avoid starving other tasks */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* ── Drain complete ────────────────────────────────────────── */
    esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_UPLOAD_ALL_DONE,
                   NULL, 0, pdMS_TO_TICKS(100));
    return sent_count;
}

/* ── Event handler (Wi-Fi connected → kick auto-upload) ──────────────── */

static void upload_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    if (event_id == RECORDER_EVENT_WIFI_CONNECTED) {
        if (s_state.cfg && s_state.cfg->auto_upload) {
            ESP_LOGI(TAG, "Wi-Fi connected — kicking auto-upload");
            upload_trigger_t trigger = TRIGGER_WIFI_CONNECTED;
            xQueueSend(s_state.cmd_queue, &trigger, 0);
        } else {
            ESP_LOGI(TAG, "Wi-Fi connected but auto_upload=false — skipping");
        }
    } else if (event_id == RECORDER_EVENT_ENQUEUED) {
        /* A recording just finished and was queued — Wi-Fi's connected
         * event only fires once (typically at boot), so without this,
         * recordings made after that point would sit queued until the
         * next reconnect or a manual Send All. */
        if (s_state.cfg && s_state.cfg->auto_upload &&
            wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "Recording enqueued — kicking auto-upload");
            upload_trigger_t trigger = TRIGGER_WIFI_CONNECTED;
            xQueueSend(s_state.cmd_queue, &trigger, 0);
        }
    }
}

/* ── Task main loop ──────────────────────────────────────────────────── */

static void upload_task_main(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Upload task started");

    /* Wait for the first trigger on the command queue.
     * The queue is created by upload_task_init() so we can block here. */
    while (true) {
        upload_trigger_t trigger;
        if (xQueueReceive(s_state.cmd_queue, &trigger, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* ── Check if a drain is already running ─────────────────── */
        if (s_state.drain_active) {
            ESP_LOGW(TAG, "Drain already active — ignoring duplicate trigger");
            continue;
        }

        /* ── Determine whether to drain ──────────────────────────── */
        bool should_drain = false;
        switch (trigger) {
        case TRIGGER_WIFI_CONNECTED:
            if (s_state.cfg && s_state.cfg->auto_upload) {
                should_drain = true;
            }
            break;
        case TRIGGER_SEND_ALL:
            /* Always drain for manual Send All */
            should_drain = true;
            break;
        }

        if (!should_drain) continue;

        /* ── Reset failed entries on explicit Send All ──────────── */
        if (trigger == TRIGGER_SEND_ALL) {
            const queue_entry_t *entries = queue_store_get_entries(
                s_state.queue, NULL);
            int count = queue_store_entry_count(s_state.queue);
            if (entries) {
                for (int i = 0; i < count; i++) {
                    if (entries[i].state == QUEUE_STATE_FAILED) {
                        /* Cast away const — we own the queue */
                        queue_store_reset_for_send_all(
                            s_state.queue,
                            (queue_entry_t *)&entries[i]);
                    }
                }
            }
        }

        /* ── Run the drain loop ──────────────────────────────────── */
        s_state.drain_active = true;
        ESP_LOGI(TAG, "Starting drain (trigger=%d, pending=%d, failed=%d)",
                 (int)trigger,
                 queue_store_count_pending(s_state.queue),
                 queue_store_count_failed(s_state.queue));

        /* Full Wi-Fi power for the duration of the drain — modem power-save
         * (the default) sleeps the radio between beacons and badly
         * throttles upload throughput. Restored once the drain is done. */
        esp_wifi_set_ps(WIFI_PS_NONE);
        int sent = upload_drain_loop();
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        s_state.drain_active = false;
        ESP_LOGI(TAG, "Drain finished — %d sent, %d pending, %d failed",
                 sent,
                 queue_store_count_pending(s_state.queue),
                 queue_store_count_failed(s_state.queue));
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void upload_task_init(const RecorderConfig *cfg, queue_index_t *queue)
{
    if (!cfg || !queue) {
        ESP_LOGE(TAG, "init: null cfg or queue");
        return;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.cfg   = cfg;
    s_state.queue = queue;

    /* Command queue: holds upload_trigger_t values */
    s_state.cmd_queue = xQueueCreate(8, sizeof(upload_trigger_t));
    if (!s_state.cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return;
    }

    /* Subscribe to Wi-Fi connected events for auto-upload, and to
     * ENQUEUED so recordings made while already connected also upload. */
    esp_event_handler_register(RECORDER_EVENTS,
                               RECORDER_EVENT_WIFI_CONNECTED,
                               upload_wifi_event_handler, NULL);
    esp_event_handler_register(RECORDER_EVENTS,
                               RECORDER_EVENT_ENQUEUED,
                               upload_wifi_event_handler, NULL);

    /* Stack in PSRAM — see s_upload_task_stack comment above. */
    s_upload_task_stack = heap_caps_malloc(UPLOAD_TASK_STACK_SIZE,
                                           MALLOC_CAP_SPIRAM);
    TaskHandle_t handle = NULL;
    if (s_upload_task_stack) {
        handle = xTaskCreateStatic(
            upload_task_main,
            "upload",
            UPLOAD_TASK_STACK_SIZE / sizeof(StackType_t),
            NULL,
            UPLOAD_TASK_PRIORITY,
            s_upload_task_stack,
            &s_upload_task_tcb
        );
    }

    if (!handle) {
        ESP_LOGE(TAG, "Failed to create upload task");
        free(s_upload_task_stack);
        s_upload_task_stack = NULL;
        vQueueDelete(s_state.cmd_queue);
        esp_event_handler_unregister(RECORDER_EVENTS,
                                     RECORDER_EVENT_WIFI_CONNECTED,
                                     upload_wifi_event_handler);
        esp_event_handler_unregister(RECORDER_EVENTS,
                                     RECORDER_EVENT_ENQUEUED,
                                     upload_wifi_event_handler);
        return;
    }

    ESP_LOGI(TAG, "Upload task initialised (auto_upload=%s)",
             cfg->auto_upload ? "true" : "false");
}

void upload_task_deinit(void)
{
    esp_event_handler_unregister(RECORDER_EVENTS,
                                 RECORDER_EVENT_WIFI_CONNECTED,
                                 upload_wifi_event_handler);
    esp_event_handler_unregister(RECORDER_EVENTS,
                                 RECORDER_EVENT_ENQUEUED,
                                 upload_wifi_event_handler);

    if (s_state.cmd_queue) {
        vQueueDelete(s_state.cmd_queue);
        s_state.cmd_queue = NULL;
    }
    /* The task self-terminates via vTaskDelete or is deleted by the caller.
     * ESP-IDF typically deletes tasks during app_main cleanup. */
    memset(&s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "Upload task de-initialised");
}

void upload_task_trigger_send_all(void)
{
    if (!s_state.cmd_queue) {
        ESP_LOGW(TAG, "trigger_send_all: task not initialised");
        return;
    }

    upload_trigger_t trigger = TRIGGER_SEND_ALL;
    if (xQueueSend(s_state.cmd_queue, &trigger, 0) != pdTRUE) {
        ESP_LOGW(TAG, "trigger_send_all: command queue full");
    }
}
