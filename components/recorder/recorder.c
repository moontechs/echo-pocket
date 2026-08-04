/** @file recorder.c
 * @brief Recording state machine — sd_writer_task consuming the ring buffer
 *        and writing WAV files to SD, plus temporary button handling.
 *
 * TODO(task-11): Remove the direct button-queue subscription in this file
 * and move start/stop decisions to ui_task.  ui_task will call
 * recorder_start() / recorder_stop() directly instead.
 */

#include "recorder.h"
#include "wav_writer.h"
#include "rec_id.h"
#include "audio_mono_ringbuf.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sd_storage.h"

/* ── Button event type (from buttons.h — we consume only the id field) ── */
/* Avoid full #include "buttons.h" to keep component deps clean;           */
/* the struct layout is stable and this file only reads `.button`.         */
typedef struct {
    int button;  /* 0=LEFT, 1=CENTER, 2=RIGHT (matches ButtonId enum)    */
} button_event_t;

#define BUTTON_CENTER  1

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "recorder";

/* ── Read chunk size (frames per iteration) ────────────────────────────
 *
 * Matches the capture task's write chunk.  Reading the same size keeps
 * the pipeline balanced.  256 frames at 16 kHz = 16 ms of audio.
 */
#define RECORDER_READ_CHUNK_FRAMES  256

/* ── Static state ────────────────────────────────────────────────────── */

static audio_mono_ringbuf_t *s_ringbuf   = NULL;
static QueueHandle_t     s_button_queue  = NULL;  /* TODO(task-11): remove */
static TaskHandle_t      s_task          = NULL;
static volatile bool     s_running       = false;

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

    bool ok = wav_writer_finalize(wav);
    wav_writer_close(wav);

    if (ok) {
        ESP_LOGI(TAG, "Recording finalized: %s", wav_writer_path(wav));
    } else {
        ESP_LOGE(TAG, "Recording finalize failed: %s", wav_writer_path(wav));
    }

    /* TODO(task-15): enqueue the completed WAV to the upload queue.
     * After queue_store is implemented, add:
     *   queue_store_enqueue(wav_writer_path(wav), wav_writer_bytes_written(wav));
     */

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

        /* ── Check for button events (TODO(task-11): remove this block) ── */
        button_event_t btn_evt;
        // TODO(task-11): Remove this direct button-queue subscription.
        // ui_task will become the sole ButtonEvent consumer and call
        // recorder_start() / recorder_stop() directly.
        while (s_button_queue &&
               xQueueReceive(s_button_queue, &btn_evt, 0) == pdTRUE) {
            if (btn_evt.button == BUTTON_CENTER) {
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
                } else if (s_state == RECORDER_STATE_RECORDING) {
                    /* ── Stop recording ───────────────────────────── */
                    ESP_LOGI(TAG, "Stop requested");
                    if (wav) {
                        finalize_recording(wav);
                        wav = NULL;
                    }
                    s_state = RECORDER_STATE_IDLE;
                    ESP_LOGI(TAG, "Recording stopped");
                }
            }
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
                        const char *old_path = wav_writer_path(wav);
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

                        ESP_LOGI(TAG, "Auto-split: new segment %s (prev: %s)",
                                 rec_id_buf, old_path);
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

    s_ringbuf = NULL;
    s_button_queue = NULL;
}

/* ── TODO(task-11): Remove this function ────────────────────────────────
 *
 * Temporary: lets app_main pass the button event queue to the recorder
 * so center-button start/stop works until ui_task takes over.
 *
 * Task 11 MUST remove:
 *   1. This function definition
 *   2. The s_button_queue static variable
 *   3. The button event processing block in sd_writer_task
 *   4. The declaration in recorder.h (if added — currently this is a
 *      .c-only function)
 */
void recorder_set_button_queue(QueueHandle_t queue)
{
    s_button_queue = queue;
}

/* ── Time sync setters (for Task 14 / boot timer) ───────────────────── */

void recorder_set_time_synced(bool synced)
{
    s_time_synced = synced;
    if (synced) {
        ESP_LOGI(TAG, "Time sync flag set — future recordings will use wall-clock timestamps");
    }
}

/* recorder_should_split() is defined in recorder_logic.c (pure, testable) */
