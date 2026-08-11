/** @file recorder.h
 * @brief Recording state machine + sd_writer_task.
 *
 * The sd_writer_task consumes mono PCM from the audio ring buffer
 * (Task 8's AFE output) and writes valid WAV files to the SD card.
 * Auto-splits at ~19 minutes.
 *
 * Button ownership lives in ui_task (Task 11).  ui_task calls
 * recorder_start() / recorder_stop() directly; the recorder task
 * receives these commands through an internal FreeRTOS queue.
 *
 * Stop lifecycle (crash-safe, per AGENTS.md §7.1):
 *   1. Close PCM stream
 *   2. Patch WAV header (RIFF/data sizes)
 *   3. fsync
 *   4. Close file
 *   5. Append to upload queue (Task 15)
 *
 * FreeRTOS priority: this task runs at HIGH - 2, below capture and AFE
 * (see audio_capture.h for the full priority table).
 */

#pragma once

#include "audio_mono_ringbuf.h"
#include "rec_id.h"
#include "recorder_split.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Forward declaration — full header is queue_store.h (storage component). */
typedef struct queue_index_s queue_index_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Default sample rate for the WAV writer (fixed at 16000 for v1.0). */
#define RECORDER_SAMPLE_RATE        16000

/** Bits per sample in the output WAV (mono s16). */
#define RECORDER_BITS_PER_SAMPLE    16

/** Number of output channels (mono after downmix/AFE). */
#define RECORDER_CHANNELS           1

/** WAV filename format: /sdcard/echo-pocket/rec/<rec_id>.wav */
#define RECORDER_FILE_PREFIX        "/sdcard/echo-pocket/rec/"
#define RECORDER_FILE_SUFFIX        ".wav"

/** Maximum path length for a recording file. */
#define RECORDER_PATH_MAX           256

/**
 * sd_writer_task stack size.
 * Measured on reference board: peak usage ~2200 bytes at 4096 with ESP-IDF
 * v5.x — but that was too tight: a stack overflow was observed on the
 * finalize→enqueue→flush path (deep FatFs/vfs call chain) once the boot-id
 * reconciliation helper's locals got inlined into this task's frame too.
 * Given internal RAM is the scarce resource on this board (see AGENTS.md
 * §Memory) rather than grow the internal-RAM budget, the stack lives in
 * PSRAM instead (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) — same pattern
 * as upload_task's HTTPS stack.
 */
#define RECORDER_TASK_STACK_SIZE    8192

/**
 * sd_writer_task priority — HIGH - 2, below capture (HIGH) and AFE (HIGH - 1).
 */
#define RECORDER_TASK_PRIORITY      (configMAX_PRIORITIES - 3)

/* ── Recorder states ─────────────────────────────────────────────────── */

typedef enum {
    RECORDER_STATE_IDLE = 0,     /**< Waiting for a start command         */
    RECORDER_STATE_RECORDING,    /**< Writing PCM to WAV, can stop/split   */
} recorder_state_t;

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialise the recorder subsystem.
 *
 * Creates the internal command queue and the sd_writer_task.
 * Does NOT start recording — the task begins in IDLE state.
 *
 * @param ringbuf  The mono audio ring buffer from audio_process_init().
 *                 The writer task reads mono PCM directly — no downmix
 *                 needed (AFE or simple 2ch→mono is done upstream).
 */
void recorder_init(audio_mono_ringbuf_t *ringbuf);

/**
 * @brief De-initialise the recorder, stopping any active recording
 *        and cleaning up the task.
 */
void recorder_deinit(void);

/**
 * @brief Request recording start (non-blocking — sends command to
 *        sd_writer_task queue).  Idempotent if already recording.
 */
void recorder_start(void);

/**
 * @brief Request recording stop (non-blocking — sends command to
 *        sd_writer_task queue).  Idempotent if already idle.
 *
 * The task will finalize the WAV (patch header, fsync, close) and
 * post RECORDER_EVENT_SAVED when done.
 */
void recorder_stop(void);

/**
 * @brief Check whether the recorder is currently recording.
 *
 * @return true if recording, false if idle.
 */
bool recorder_is_recording(void);

/**
 * @brief Set the upload queue store for enqueueing completed recordings.
 *
 * Called once at boot (Task 19 / app_main) after queue_store_init().
 * When set, finalize_recording() enqueues a QUEUE_STATE_PENDING entry
 * after the WAV is fsync'd and closed.
 */
void recorder_set_queue_store(queue_index_t *queue);

/**
 * @brief Notify the recorder that SNTP has synced the system clock.
 *
 * Called by Task 14 (Wi-Fi manager) after a successful SNTP sync.
 * When true, subsequent recording IDs use wall-clock timestamps
 * instead of the boot-relative fallback.
 */
void recorder_set_time_synced(bool synced);

#ifdef __cplusplus
}
#endif
