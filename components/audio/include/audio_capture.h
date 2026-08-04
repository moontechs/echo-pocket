/** @file audio_capture.h
 * @brief I2S audio capture subsystem — ES7210 codec init, I2S RX, capture task.
 *
 * ── FreeRTOS task priority order (documented here for enforcement) ─────
 *
 *   Priority  | Task                  | Source
 *   ──────────┼───────────────────────┼─────────────────────
 *   HIGH      | audio_capture_task    | audio_capture.c (here)
 *   HIGH - 1  | audio_process_task    | audio_process.c (Task 8)
 *   HIGH - 2  | sd_writer_task        | recorder.c (Task 7)
 *   NORMAL    | ui_task               | ui_task.c (Task 11)
 *   NORMAL - 1| wifi_task             | wifi_manager.c (Task 14)
 *   NORMAL - 2| upload_task           | upload_task.c (Task 17)
 *
 * Capture must never block on SD, display, or network — this priority
 * ordering is what makes that guarantee real, not just prose.  Any new
 * task created in later tasks MUST stay below capture's priority.
 *
 * Stack sizes are measured per-task and noted in the corresponding
 * implementation file, not guessed here.
 */

#pragma once

#include "esp_err.h"
#include "audio_ringbuf.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Capture constants ───────────────────────────────────────────────── */

/** Default capture sample rate — fixed at 16000 Hz for v1.0.                */
#define AUDIO_CAPTURE_SAMPLE_RATE   16000

/** Number of I2S channels (2 mics → stereo interleaved).                    */
#define AUDIO_CAPTURE_CHANNELS      2

/** Bits per sample.                                                         */
#define AUDIO_CAPTURE_BITS_PER_SAMPLE  16

/**
 * Ring buffer size in stereo frames.
 *
 * 4 seconds at 16000 Hz = 64000 frames → ~256 KB of PSRAM.
 * Budget check in sdkconfig.defaults confirms this fits easily in 8 MB PSRAM.
 */
#define AUDIO_CAPTURE_RINGBUF_FRAMES  (AUDIO_CAPTURE_SAMPLE_RATE * 4)

/** I2S DMA buffer size in bytes — must be a multiple of the DMA alignment.  */
#define AUDIO_CAPTURE_I2S_DMA_BUF_SIZE  4096

/** Number of I2S DMA buffers (double-buffered for continuous streaming).    */
#define AUDIO_CAPTURE_I2S_DMA_BUF_COUNT 4

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the ES7210 codec (over I2C), I2S RX channel,
 *        and allocate the PSRAM ring buffer.
 *
 * Call once at boot, before audio_capture_start().
 *
 * @param[out] out_rb  Receives a pointer to the ring buffer so downstream
 *                     consumers (AFE / writer) can read from it.
 * @return  ESP_OK on success, or an ESP_ERR_* code.
 */
esp_err_t audio_capture_init(audio_ringbuf_t **out_rb);

/**
 * @brief Create and launch the high-priority FreeRTOS capture task.
 *
 * The task reads I2S frames in a loop and writes them into the ring buffer
 * provided by audio_capture_init().
 *
 * @return  ESP_OK on success.
 */
esp_err_t audio_capture_start(void);

/**
 * @brief Stop the capture task and disable I2S.
 *
 * @return  ESP_OK on success.
 */
esp_err_t audio_capture_stop(void);

#ifdef __cplusplus
}
#endif
