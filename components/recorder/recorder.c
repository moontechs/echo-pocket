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
#include "audio_capture.h"
#include "audio_process.h"
#include "device_events.h"
#include "queue_store.h"
#include "battery.h"
#include "board.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sd_storage.h"
#include "esp_heap_caps.h"

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "recorder";

/* ── Internal command type ───────────────────────────────────────────── */

typedef enum {
    RECORDER_CMD_START = 0,
    RECORDER_CMD_STOP,
    RECORDER_CMD_RECONCILE_BOOT_IDS, /**< Time just synced — fix up REC_BOOT_ ids */
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

/* Task stack lives in PSRAM (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) —
 * internal RAM is the scarce resource on this board (see AGENTS.md
 * §Memory), same pattern as upload_task's HTTPS stack. */
static StaticTask_t s_task_tcb;
static StackType_t *s_task_stack = NULL;

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

/**
 * Resolve a "REC_BOOT_<uptime_s>_NNN" id into a proper wall-clock rec_id,
 * given the current time and uptime. Reuses rec_id_generate's synced path
 * rather than re-deriving the timestamp format.
 */
static bool resolve_boot_id(const char *boot_id, time_t now, uint32_t now_uptime_s,
                            char *buf, size_t buf_size)
{
    uint32_t rec_uptime_s, counter;
    if (!boot_id ||
        sscanf(boot_id, "REC_BOOT_%9" SCNu32 "_%3" SCNu32, &rec_uptime_s, &counter) != 2) {
        return false;
    }
    if (rec_uptime_s > now_uptime_s) {
        return false; /* impossible offset — leave the id alone */
    }
    time_t rec_time = now - (time_t)(now_uptime_s - rec_uptime_s);
    return rec_id_generate(buf, buf_size, true, rec_time, 0, counter) == REC_ID_OK;
}

/**
 * Fix up any queued recordings still carrying a boot-relative id now that
 * the wall clock is known — otherwise a recording made offline keeps its
 * meaningless "REC_BOOT_..." name forever, even once uploaded.  Only
 * touches PENDING/FAILED entries (never UPLOADING/SENT/RECORDING).
 */
static void reconcile_boot_ids(void)
{
    if (!s_queue) return;

    time_t now = time(NULL);
    uint32_t now_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);

    int count = 0;
    const queue_entry_t *entries = queue_store_get_entries(s_queue, &count);
    if (!entries) return;

    for (int i = 0; i < count; i++) {
        const queue_entry_t *e = &entries[i];
        if (e->state != QUEUE_STATE_PENDING && e->state != QUEUE_STATE_FAILED) {
            continue;
        }
        if (strncmp(e->id, REC_ID_BOOT_PREFIX, strlen(REC_ID_BOOT_PREFIX)) != 0) {
            continue;
        }

        char new_id[REC_ID_MAX_LEN];
        if (!resolve_boot_id(e->id, now, now_uptime_s, new_id, sizeof(new_id))) {
            ESP_LOGW(TAG, "Could not resolve boot id %s — leaving as-is", e->id);
            continue;
        }

        char new_path[RECORDER_PATH_MAX];
        build_path(new_path, sizeof(new_path), new_id);

        if (rename(e->file, new_path) != 0) {
            ESP_LOGW(TAG, "Failed to rename %s -> %s — leaving boot id unresolved",
                     e->file, new_path);
            continue;
        }

        queue_store_err_t qerr = queue_store_rename_entry(
            s_queue, (queue_entry_t *)e, new_id, new_path);
        if (qerr != QUEUE_STORE_OK) {
            ESP_LOGE(TAG, "Failed to persist renamed entry %s: %s",
                     new_id, queue_store_err_str(qerr));
        } else {
            ESP_LOGI(TAG, "Resolved offline recording %s -> %s", e->id, new_id);
        }
    }
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
                    /* ── Power up the mic — it's off while idle ──────
                     * Only listening while a recording is in progress. */
                    esp_err_t cap_err = audio_capture_start();
                    if (cap_err != ESP_OK) {
                        ESP_LOGE(TAG, "audio_capture_start failed: %s",
                                 esp_err_to_name(cap_err));
                        continue;
                    }
                    esp_err_t proc_err = audio_process_start();
                    if (proc_err != ESP_OK) {
                        ESP_LOGE(TAG, "audio_process_start failed: %s",
                                 esp_err_to_name(proc_err));
                        audio_capture_stop();
                        continue;
                    }

                    /* ── Start recording ──────────────────────────── */
                    rec_id_err_t id_err = generate_next_id(rec_id_buf,
                                                           sizeof(rec_id_buf));
                    if (id_err != REC_ID_OK) {
                        ESP_LOGE(TAG, "Failed to generate rec ID: %s",
                                 rec_id_err_str(id_err));
                        audio_process_stop();
                        audio_capture_stop();
                        continue;
                    }

                    build_path(path_buf, sizeof(path_buf), rec_id_buf);
                    wav = wav_writer_open(path_buf,
                                          RECORDER_CHANNELS,
                                          RECORDER_SAMPLE_RATE,
                                          RECORDER_BITS_PER_SAMPLE);
                    if (!wav) {
                        ESP_LOGE(TAG, "Failed to open WAV: %s", path_buf);
                        audio_process_stop();
                        audio_capture_stop();
                        continue;
                    }

                    /* Mic just powered on, so the ring buffer should
                     * already be empty — discard defensively in case any
                     * samples landed before this point. */
                    audio_mono_ringbuf_discard_available(s_ringbuf);

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

                    /* Power the mic back down — nothing should be
                     * listening between recordings. */
                    audio_process_stop();
                    audio_capture_stop();
                    ESP_LOGI(TAG, "Recording stopped");
                }
            } else if (cmd == RECORDER_CMD_RECONCILE_BOOT_IDS) {
                /* Runs here (not inline in the SNTP callback) because it
                 * does SD file I/O — the LWIP task that SNTP calls back on
                 * has only a 3KB stack, too little for rename()/JSON
                 * flush. This task's stack is sized for SD I/O already. */
                reconcile_boot_ids();
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
            audio_process_stop();
            audio_capture_stop();
            board_power_off();
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
                            audio_process_stop();
                            audio_capture_stop();
                            continue;
                        }

                        /* Generate new ID and open next segment */
                        rec_id_err_t id_err = generate_next_id(rec_id_buf,
                                                               sizeof(rec_id_buf));
                        if (id_err != REC_ID_OK) {
                            ESP_LOGE(TAG, "Auto-split: ID gen failed");
                            s_state = RECORDER_STATE_IDLE;
                            audio_process_stop();
                            audio_capture_stop();
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
                            audio_process_stop();
                            audio_capture_stop();
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
        audio_process_stop();
        audio_capture_stop();
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

    s_task_stack = heap_caps_malloc(RECORDER_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_task_stack) {
        s_running = false;
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        ESP_LOGE(TAG, "Failed to allocate writer task stack (PSRAM)");
        return;
    }

    s_task = xTaskCreateStatic(
        sd_writer_task,
        "sd_writer",
        RECORDER_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        RECORDER_TASK_PRIORITY,
        s_task_stack,
        &s_task_tcb
    );

    if (!s_task) {
        s_running = false;
        free(s_task_stack);
        s_task_stack = NULL;
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

        /* Defer the actual reconciliation work to sd_writer_task — this
         * setter is called from the SNTP callback, which runs on the LWIP
         * task's tiny (3KB) stack. Just enqueueing a command is cheap. */
        if (s_cmd_queue) {
            recorder_cmd_t cmd = RECORDER_CMD_RECONCILE_BOOT_IDS;
            if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
                ESP_LOGW(TAG, "set_time_synced: command queue full, dropping reconcile");
            }
        }
    }
}

/* recorder_should_split() is defined in recorder_logic.c (pure, testable) */
