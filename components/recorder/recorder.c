/** @file recorder.c
 * @brief Recording state machine — sd_writer_task consuming the ring buffer
 *        and writing WAV files to SD.
 *
 * Button ownership has moved to ui_task (Task 11).  ui_task calls
 * recorder_start() / recorder_stop() directly; the sd_writer_task
 * receives commands through an internal FreeRTOS queue.
 */

#include "recorder.h"
#include "wav_writer.h"
#include "rec_id.h"
#include "audio_mono_ringbuf.h"
#include "device_events.h"
#include "queue_store.h"
#include "battery.h"

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sd_storage.h"

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "recorder";

/* ── Internal command type ───────────────────────────────────────────── */

typedef enum {
    RECORDER_CMD_START = 0,
    RECORDER_CMD_STOP,
} recorder_cmd_t;

/* ── Read chunk size (frames per iteration) ────────────────────────────
 *
 * Matches the capture task's write chunk.  Reading the same size keeps
 * the pipeline balanced.  256 frames at 16 kHz = 16 ms of audio.
 */
#define RECORDER_READ_CHUNK_FRAMES  256

/* ── Static state ────────────────────────────────────────────────────── */

static audio_mono_ringbuf_t *s_ringbuf   = NULL;
static QueueHandle_t     s_cmd_queue     = NULL;
static TaskHandle_t      s_task          = NULL;
static volatile bool     s_running       = false;

/* Upload queue handle — set by recorder_set_queue_store() before use.    */
static queue_index_t *s_queue = NULL;

/* These are updated by the task loop, read by recorder_is_recording().    */
static volatile recorder_state_t s_state = RECORDER_STATE_IDLE;

/* Per-boot recording sequence counter — starts at 0, increments each new
 * recording (including auto-splits).  Wraps to 0 after 999.              */
static uint32_t s_rec_counter  = 0;

/* Time sync state — set by Task 14, consumed when generating rec_ids.    */
static volatile bool     s_time_synced   = false;
static volatile uint32_t s_boot_uptime_s = 0;   /* updated via timer     */

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Build the full WAV file path from a recording ID. */
static void build_path(char *buf, size_t buf_size, const char *rec_id)
{
    snprintf(buf, buf_size, RECORDER_FILE_PREFIX "%s" RECORDER_FILE_SUFFIX, rec_id);
}

/** Generate the next recording ID, advancing s_rec_counter. */
static rec_id_err_t generate_next_id(char *buf, size_t buf_size)
{
    time_t now = time(NULL);
    rec_id_err_t err = rec_id_generate(buf, buf_size,
                                       s_time_synced,
                                       now,
                                       s_boot_uptime_s,
                                       s_rec_counter);
    if (err == REC_ID_OK) {
        s_rec_counter++;
        if (s_rec_counter > 999) {
            s_rec_counter = 0;
        }
    }
    return err;
}

/* ── Auto-split logic ────────────────────────────────────────────────── */

/**
 * Check whether the current WAV file should be auto-split.
 * Delegates to the pure recorder_should_split() for testability.
 */
static bool should_auto_split(const wav_writer_t *wav)
{
    if (!wav) return false;
    return recorder_should_split(wav_writer_bytes_written(wav));
}

/* ── Finalize path (shared by normal stop, auto-split, low-battery) ──── */

/**
 * Finalize a WAV file: patch header + fsync + close.
 * This is the single finalize path reused by:
 *   - Normal stop (Task 7)
 *   - Auto-split (Task 7)
 *   - Low-battery safe-stop (Task 18)
 *
 * After this returns, the WAV is durably on SD and can be enqueued.
 *
 * @return true on success.
 */
static bool finalize_recording(wav_writer_t *wav)
{
    if (!wav) return false;

    /* Save path and size BEFORE close — close() frees the wav struct. */
    char path_buf[RECORDER_PATH_MAX];
    const char *raw_path = wav_writer_path(wav);
    if (raw_path) {
        strncpy(path_buf, raw_path, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
    } else {
        path_buf[0] = '\0';
    }
    uint32_t saved_size = wav_writer_bytes_written(wav);

    bool ok = wav_writer_finalize(wav);
    wav_writer_close(wav);

    if (ok) {
        ESP_LOGI(TAG, "Recording finalized: %s", path_buf);
        /* Notify UI that the recording has been saved */
        esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_SAVED, NULL, 0, 0);
    } else {
        ESP_LOGE(TAG, "Recording finalize failed: %s", path_buf);
    }

    /* ── Enqueue the completed WAV to the upload queue ───────────── */
    if (ok && s_queue) {
        /* Use saved path and size (wav struct is now freed) */
        uint32_t size = saved_size;

        /* Compute duration from byte count:
         *   mono 16 kHz s16 → 32000 bytes/sec → ms = bytes / 32 */
        uint32_t duration_ms = size / 32;

        /* Extract the recording ID from the path (e.g.
         * "/sdcard/echo-pocket/rec/REC_20260804_215700_001.wav"
         *  → "REC_20260804_215700_001") */
        const char *prefix = RECORDER_FILE_PREFIX;
        const char *suffix = RECORDER_FILE_SUFFIX;
        size_t prefix_len = strlen(prefix);
        size_t suffix_len = strlen(suffix);
        char rec_id[REC_ID_MAX_LEN];

        const char *start = path_buf + prefix_len;
        const char *end = strstr(start, suffix);
        size_t id_len = (end && end > start) ? (size_t)(end - start)
                                              : strlen(start);
        if (id_len >= sizeof(rec_id)) id_len = sizeof(rec_id) - 1;
        memcpy(rec_id, start, id_len);
        rec_id[id_len] = '\0';

        queue_store_err_t q_err = queue_store_enqueue(
            s_queue, rec_id, path_buf, duration_ms, size);

        if (q_err == QUEUE_STORE_OK) {
            ESP_LOGI(TAG, "Enqueued %s for upload", rec_id);
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_ENQUEUED,
                           NULL, 0, 0);
        } else {
            ESP_LOGE(TAG, "Failed to enqueue %s: %s",
                     rec_id, queue_store_err_str(q_err));
        }
    }

    return ok;
}

/* ── Main writer task ────────────────────────────────────────────────── */

static void sd_writer_task(void *arg)
{
    (void)arg;

    wav_writer_t *wav = NULL;
    char rec_id_buf[REC_ID_MAX_LEN];
    char path_buf[RECORDER_PATH_MAX];

    /* Local PCM buffer — stack-allocated, sized to the read chunk.
     * Receives mono PCM directly from the process task's output — no
     * stereo buffer or downmix needed (AFE does that upstream).          */
    int16_t mono_buf[RECORDER_READ_CHUNK_FRAMES];  /* mono PCM from AFE   */

    ESP_LOGI(TAG, "Writer task started (prio %d, stack %d)",
             (int)RECORDER_TASK_PRIORITY, (int)RECORDER_TASK_STACK_SIZE);

    /* Periodically update boot uptime — this runs in the task loop
     * so it doesn't need its own timer.  Read esp_timer once per loop.    */
    uint32_t last_uptime_update = 0;

    while (s_running) {
        /* ── Update boot uptime (throttled to ~1 Hz) ───────────────── */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t uptime_s = now_ms / 1000;
        if (uptime_s != last_uptime_update) {
            s_boot_uptime_s = uptime_s;
            last_uptime_update = uptime_s;
        }

        /* ── Check for recorder commands (sent by ui_task) ──────────── */
        recorder_cmd_t cmd;
        while (s_cmd_queue &&
               xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd == RECORDER_CMD_START) {
                if (s_state == RECORDER_STATE_IDLE) {
                    /* ── Start recording ──────────────────────────── */
                    rec_id_err_t id_err = generate_next_id(rec_id_buf,
                                                           sizeof(rec_id_buf));
                    if (id_err != REC_ID_OK) {
                        ESP_LOGE(TAG, "Failed to generate rec ID: %s",
                                 rec_id_err_str(id_err));
                        continue;
                    }

                    build_path(path_buf, sizeof(path_buf), rec_id_buf);
                    wav = wav_writer_open(path_buf,
                                          RECORDER_CHANNELS,
                                          RECORDER_SAMPLE_RATE,
                                          RECORDER_BITS_PER_SAMPLE);
                    if (!wav) {
                        ESP_LOGE(TAG, "Failed to open WAV: %s", path_buf);
                        continue;
                    }

                    s_state = RECORDER_STATE_RECORDING;
                    ESP_LOGI(TAG, "Recording started: %s", rec_id_buf);

                    /* Notify UI */
                    esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_STARTED,
                                   NULL, 0, 0);
                }
            } else if (cmd == RECORDER_CMD_STOP) {
                if (s_state == RECORDER_STATE_RECORDING) {
                    /* ── Stop recording ───────────────────────────── */
                    ESP_LOGI(TAG, "Stop requested");

                    /* Notify UI that stop has begun (Saving state) */
                    esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_STOPPED,
                                   NULL, 0, 0);

                    if (wav) {
                        finalize_recording(wav);
                        wav = NULL;
                    }
                    s_state = RECORDER_STATE_IDLE;
                    ESP_LOGI(TAG, "Recording stopped");
                }
            }
        }

        /* ── Critical battery safe-stop check ────────────────────
         * Runs once per iteration; reuses the same finalize path as
         * normal stop and auto-split.  No duplicated logic. */
        if (s_state == RECORDER_STATE_RECORDING && battery_is_critical()) {
            ESP_LOGW(TAG, "Critical battery — safe-stopping recording");
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_STOPPED,
                           NULL, 0, 0);
            if (wav) {
                finalize_recording(wav);
                wav = NULL;
            }
            s_state = RECORDER_STATE_IDLE;
            /* Power down — if the hardware supports it */
            /* TODO: call board_power_off() once that API is added */
        }

        /* ── If recording, consume mono PCM from process output ────── */
        if (s_state == RECORDER_STATE_RECORDING && wav) {
            size_t avail = audio_mono_ringbuf_available(s_ringbuf);
            if (avail > 0) {
                size_t to_read = (avail < RECORDER_READ_CHUNK_FRAMES)
                                 ? avail : RECORDER_READ_CHUNK_FRAMES;

                size_t read = audio_mono_ringbuf_read(s_ringbuf, mono_buf, to_read);
                if (read > 0) {
                    /* Mono PCM is already processed by the AFE (Task 8)
                     * — write directly, no downmix needed.              */
                    wav_writer_write(wav, mono_buf, read);

                    /* ── Check auto-split ──────────────────────────── */
                    if (should_auto_split(wav)) {
                        ESP_LOGI(TAG, "Auto-split at %" PRIu32 " bytes",
                                 wav_writer_bytes_written(wav));

                        /* Finalize current, open new — reuses the
                         * finalize-then-reopen path from normal stop.   */
                        bool ok = finalize_recording(wav);
                        wav = NULL;

                        if (!ok) {
                            ESP_LOGE(TAG, "Auto-split: finalize failed");
                            s_state = RECORDER_STATE_IDLE;
                            continue;
                        }

                        /* Generate new ID and open next segment */
                        rec_id_err_t id_err = generate_next_id(rec_id_buf,
                                                               sizeof(rec_id_buf));
                        if (id_err != REC_ID_OK) {
                            ESP_LOGE(TAG, "Auto-split: ID gen failed");
                            s_state = RECORDER_STATE_IDLE;
                            continue;
                        }

                        build_path(path_buf, sizeof(path_buf), rec_id_buf);
                        wav = wav_writer_open(path_buf,
                                              RECORDER_CHANNELS,
                                              RECORDER_SAMPLE_RATE,
                                              RECORDER_BITS_PER_SAMPLE);
                        if (!wav) {
                            ESP_LOGE(TAG, "Auto-split: open failed for %s",
                                     path_buf);
                            s_state = RECORDER_STATE_IDLE;
                            continue;
                        }

                        ESP_LOGI(TAG, "Auto-split: new segment %s", rec_id_buf);
                        /* Notify UI of new recording segment */
                        esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_STARTED,
                                       NULL, 0, 0);
                    }
                }
            } else {
                /* Ring buffer empty — yield to let the capture task fill it */
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        } else {
            /* Idle — sleep briefly to avoid busy-waiting */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    /* ── Cleanup on task exit ──────────────────────────────────────── */
    if (wav) {
        ESP_LOGW(TAG, "Writer task exiting with active recording — finalizing");
        finalize_recording(wav);
        wav = NULL;
        s_state = RECORDER_STATE_IDLE;
    }

    ESP_LOGI(TAG, "Writer task stopped");
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void recorder_init(audio_mono_ringbuf_t *ringbuf)
{
    if (!ringbuf) {
        ESP_LOGE(TAG, "recorder_init: ringbuf is NULL");
        return;
    }

    s_ringbuf = ringbuf;
    s_state   = RECORDER_STATE_IDLE;
    s_running = true;

    /* Create internal command queue for ui_task → recorder IPC */
    s_cmd_queue = xQueueCreate(4, sizeof(recorder_cmd_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create recorder command queue");
        s_running = false;
        return;
    }

    BaseType_t created = xTaskCreate(
        sd_writer_task,
        "sd_writer",
        RECORDER_TASK_STACK_SIZE,
        NULL,
        RECORDER_TASK_PRIORITY,
        &s_task
    );

    if (created != pdPASS) {
        s_running = false;
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        ESP_LOGE(TAG, "Failed to create writer task");
        return;
    }

    ESP_LOGI(TAG, "Recorder initialized (split at %" PRIu32 " bytes ≈ %d min)",
             RECORDER_SPLIT_BYTES, (int)(RECORDER_SPLIT_BYTES / (16000 * 2 * 60)));
}

void recorder_deinit(void)
{
    s_running = false;

    /* Give the task time to exit its loop and finalize any active recording */
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_task = NULL;
    }

    if (s_cmd_queue) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }

    s_ringbuf = NULL;
}

void recorder_start(void)
{
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "recorder_start: not initialized");
        return;
    }

    recorder_cmd_t cmd = RECORDER_CMD_START;
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "recorder_start: command queue full");
    }
}

void recorder_stop(void)
{
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "recorder_stop: not initialized");
        return;
    }

    recorder_cmd_t cmd = RECORDER_CMD_STOP;
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "recorder_stop: command queue full");
    }
}

bool recorder_is_recording(void)
{
    return s_state == RECORDER_STATE_RECORDING;
}

/* ── Time sync setters (for Task 14 / boot timer) ───────────────────── */

void recorder_set_queue_store(queue_index_t *queue)
{
    s_queue = queue;
}

void recorder_set_time_synced(bool synced)
{
    s_time_synced = synced;
    if (synced) {
        ESP_LOGI(TAG, "Time sync flag set — future recordings will use wall-clock timestamps");
    }
}

/* recorder_should_split() is defined in recorder_logic.c (pure, testable) */
