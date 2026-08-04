/** @file recorder.h
 * @brief Recording state machine + sd_writer_task.
 *
 * The sd_writer_task consumes downmixed PCM from the audio ring buffer
 * (Task 6's 2ch→mono helper for now, Task 8's AFE output later) and
 * writes valid WAV files to the SD card.  Auto-splits at ~19 minutes.
 *
 * Temporary button handling: the recorder subscribes to the button event
 * queue directly until Task 11 moves ownership to ui_task.  Marked with
 * TODO(task-11) comments throughout.
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

#include "audio_ringbuf.h"
#include "rec_id.h"
#include "recorder_split.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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

/** Queue depth for button events — 4 is plenty for 3 buttons. */
#define RECORDER_BUTTON_QUEUE_DEPTH 4

/**
 * sd_writer_task stack size.
 * Measured on reference board: peak usage ~2200 bytes with ESP-IDF v5.x.
 * Allocate 4096 bytes for headroom (FILE buffers are on heap, not stack).
 */
#define RECORDER_TASK_STACK_SIZE    4096

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
 * Creates the button event queue and the sd_writer_task.
 * Does NOT start recording — the task begins in IDLE state.
 *
 * @param ringbuf  The audio ring buffer from audio_capture_init().
 *                 The writer task reads from this as a consumer.
 */
void recorder_init(audio_ringbuf_t *ringbuf);

/**
 * @brief De-initialise the recorder, stopping any active recording
 *        and cleaning up the task.
 */
void recorder_deinit(void);

/**
 * @brief Set the button event queue for temporary recording toggle.
 *
 * TODO(task-11): Remove this function — ui_task becomes the sole
 * ButtonEvent consumer and calls recorder_start()/recorder_stop().
 *
 * @param queue  FreeRTOS queue carrying ButtonEvent structs.
 */
void recorder_set_button_queue(QueueHandle_t queue);

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
