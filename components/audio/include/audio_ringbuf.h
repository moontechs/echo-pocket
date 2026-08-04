/** @file audio_ringbuf.h
 * @brief PSRAM-backed ring buffer for raw I2S audio frames.
 *
 * Single-producer, single-consumer design.  The capture task writes
 * interleaved 2-channel s16 PCM; one downstream consumer reads.
 *
 * Overflow policy: when the buffer is full, the oldest frame is dropped
 * and overflow_count increments — capture never blocks.
 *
 * This is pure C logic with no ESP-IDF or FreeRTOS dependencies so
 * it can be unit-tested under logic_tests without hardware.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Ring buffer struct ──────────────────────────────────────────────── */

typedef struct {
    int16_t *buffer;             /**< Sample storage (2ch interleaved)     */
    size_t   capacity_frames;    /**< Max interleaved frame pairs          */
    volatile size_t write_pos;   /**< Monotonic write cursor (samples)     */
    volatile size_t read_pos;    /**< Monotonic read cursor (samples)      */
    volatile uint32_t overflow_count; /**< Frames dropped on overflow      */
} audio_ringbuf_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a ring buffer in PSRAM.
 *
 * @p capacity_frames  Max number of *interleaved stereo frames* (each frame
 *                     is 2 × int16_t samples).  Must be > 0.
 * @return  Initialised ring buffer, or NULL on allocation failure.
 *          Caller must free with audio_ringbuf_free().
 */
audio_ringbuf_t *audio_ringbuf_alloc(size_t capacity_frames);

/**
 * @brief Free a ring buffer allocated by audio_ringbuf_alloc().
 * Safe to call with NULL.
 */
void audio_ringbuf_free(audio_ringbuf_t *rb);

/**
 * @brief Write interleaved 2-channel PCM frames into the ring buffer.
 *
 * Non-blocking — if there isn't enough space, the oldest frames are
 * silently dropped and overflow_count is incremented.
 *
 * @p data   Interleaved stereo s16 samples (2 × @p frames values).
 * @p frames Number of stereo frames to write.
 * @return   Number of frames actually written (= frames unless allocation
 *           failed, in which case 0).
 */
size_t audio_ringbuf_write(audio_ringbuf_t *rb, const int16_t *data, size_t frames);

/**
 * @brief Read interleaved 2-channel PCM frames from the ring buffer.
 *
 * Non-blocking — returns whatever is available, up to @p max_frames.
 *
 * @p buf        Destination buffer (must hold 2 × @p max_frames samples).
 * @p max_frames Maximum stereo frames to read.
 * @return       Number of frames actually read (0 if buffer is empty).
 */
size_t audio_ringbuf_read(audio_ringbuf_t *rb, int16_t *buf, size_t max_frames);

/**
 * @brief Return the number of stereo frames currently available to read.
 */
size_t audio_ringbuf_available(const audio_ringbuf_t *rb);

/**
 * @brief Return and optionally reset the overflow counter.
 *
 * @p reset  If true, the counter is zeroed after reading.
 * @return    Number of frames dropped since last reset.
 */
uint32_t audio_ringbuf_get_overflow(audio_ringbuf_t *rb, bool reset);

/**
 * @brief Return the ring buffer's capacity in stereo frames.
 */
size_t audio_ringbuf_capacity(const audio_ringbuf_t *rb);

/* ── Downmix helper ──────────────────────────────────────────────────── */

/**
 * @brief Downmix interleaved 2ch s16 PCM to mono s16 by averaging.
 *
 * @p stereo  Interleaved L/R samples (2 × @p frames values).
 * @p mono    Output buffer (must hold @p frames values).
 * @p frames  Number of stereo frames to downmix.
 */
void audio_downmix_2ch_to_mono(const int16_t *stereo, int16_t *mono, size_t frames);

#ifdef __cplusplus
}
#endif
