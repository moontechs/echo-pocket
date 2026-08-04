/** @file audio_process.h
 * @brief Audio processing task — ESP-SR AFE downstream of the capture
 *        ring buffer, producing mono PCM + voice level for the UI.
 *
 * ── FreeRTOS task priority ─────────────────────────────────────────────
 *
 * Priority   | Task                  | Source
 * ───────────┼───────────────────────┼─────────────────────
 * HIGH       | audio_capture_task    | audio_capture.c
 * HIGH - 1   | audio_process_task    | audio_process.c (here)
 * HIGH - 2   | sd_writer_task        | recorder.c (Task 7)
 * NORMAL     | ui_task               | ui_task.c (Task 11)
 * NORMAL - 1 | wifi_task             | wifi_manager.c (Task 14)
 * NORMAL - 2 | upload_task           | upload_task.c (Task 17)
 *
 * This task reads 2-channel frames from the capture ring buffer,
 * runs NS + VAD + AGC (ESP-SR AFE), and writes mono PCM to an output
 * ring buffer.  Capture (Task 6) is unaffected — it still only ever
 * writes into the capture ring buffer.
 */

#pragma once

#include "audio_ringbuf.h"
#include "audio_mono_ringbuf.h"
#include "config.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Sample rate — must match AUDIO_CAPTURE_SAMPLE_RATE (16000). */
#define AUDIO_PROCESS_SAMPLE_RATE      16000

/** Output ring buffer size in mono samples (4 seconds × 16 kHz). */
#define AUDIO_PROCESS_OUTPUT_BUF_SAMPLES  (AUDIO_PROCESS_SAMPLE_RATE * 4)

/** Chunk size in stereo frames processed per AFE feed iteration.
 *  256 frames at 16 kHz = 16 ms — matches the capture task's read chunk. */
#define AUDIO_PROCESS_CHUNK_FRAMES      256

/** Voice level EMA smoothing alpha (tuned for 16 ms update period).
 *  0.15 means the level adapts in ~100 ms (time constant ≈ 6 * 16 ms). */
#define AUDIO_PROCESS_LEVEL_ALPHA       0.15f

/** Process task stack size (bytes).
 *  Measured on reference board: peak ~2400 bytes with ESP-SR.
 *  Allocate 4096 for headroom. */
#define AUDIO_PROCESS_TASK_STACK_SIZE   4096

/** Process task priority — HIGH - 1, below capture (HIGH). */
#define AUDIO_PROCESS_TASK_PRIORITY     (configMAX_PRIORITIES - 2)

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the audio processing subsystem.
 *
 * Creates the ESP-SR AFE handle (if enabled in config) and allocates
 * the mono output ring buffer.  Does NOT start the task — call
 * audio_process_start() after this.
 *
 * @param input_rb   The capture ring buffer (2ch stereo interleaved).
 * @param cfg        Recorder configuration (gates NS/VAD/AGC).
 * @param[out] out_rb Receives the mono output ring buffer for downstream
 *                    consumers (sd_writer_task).
 * @return  ESP_OK on success.
 */
esp_err_t audio_process_init(audio_ringbuf_t *input_rb,
                             const RecorderConfig *cfg,
                             audio_mono_ringbuf_t **out_rb);

/**
 * @brief Create and launch the audio process FreeRTOS task.
 *
 * @return  ESP_OK on success.
 */
esp_err_t audio_process_start(void);

/**
 * @brief Stop the process task and release ESP-SR resources.
 *
 * @return  ESP_OK on success.
 */
esp_err_t audio_process_stop(void);

/**
 * @brief Get the current smoothed voice level.
 *
 * Updated every chunk (~16 ms) by the process task.
 * Thread-safe: reads a volatile float, no lock needed.
 *
 * @return  Level in [0.0, 1.0].
 */
float audio_process_get_voice_level(void);

/**
 * @brief Get whether VAD currently detects voice activity.
 *
 * Updated every chunk by the process task; reflects the AFE's
 * VAD decision when VAD is enabled, or a simple energy threshold
 * when VAD is disabled.
 *
 * @return  true if voice is detected in the current chunk.
 */
bool audio_process_is_voice_active(void);

#ifdef __cplusplus
}
#endif
