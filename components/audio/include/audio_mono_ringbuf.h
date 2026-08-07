/** @file audio_mono_ringbuf.h
 * @brief PSRAM-backed ring buffer for mono int16_t PCM samples.
 *
 * Single-producer, single-consumer design.  The audio process task
 * writes mono AFE output; the sd_writer_task reads.
 *
 * Overflow policy: when the buffer is full, the oldest samples are
 * dropped and overflow_count increments — the process task never
 * blocks on a slow consumer.
 *
 * Pure C logic, no ESP-IDF or FreeRTOS dependencies — unit-testable
 * under logic_tests.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mono ring buffer struct ─────────────────────────────────────────── */

typedef struct {
    int16_t *buffer;              /**< Sample storage (mono)               */
    size_t   capacity_samples;    /**< Max mono samples                    */
    volatile size_t write_pos;    /**< Monotonic write cursor (samples)    */
    volatile size_t read_pos;     /**< Monotonic read cursor (samples)     */
    volatile uint32_t overflow_count; /**< Samples dropped on overflow     */
} audio_mono_ringbuf_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a mono ring buffer in PSRAM.
 *
 * @param capacity_samples  Max number of mono int16_t samples.  Must be > 0.
 * @return  Initialised ring buffer, or NULL on allocation failure.
 *          Caller must free with audio_mono_ringbuf_free().
 */
audio_mono_ringbuf_t *audio_mono_ringbuf_alloc(size_t capacity_samples);

/**
 * @brief Free a mono ring buffer.  Safe to call with NULL.
 */
void audio_mono_ringbuf_free(audio_mono_ringbuf_t *rb);

/**
 * @brief Write mono int16_t samples into the ring buffer.
 *
 * Non-blocking — if there isn't enough space, the oldest samples are
 * dropped and overflow_count is incremented.
 *
 * @param data     Mono int16_t samples.
 * @param count    Number of samples to write.
 * @return         Number of samples actually written.
 */
size_t audio_mono_ringbuf_write(audio_mono_ringbuf_t *rb,
                                const int16_t *data, size_t count);

/**
 * @brief Read mono int16_t samples from the ring buffer.
 *
 * Non-blocking — returns whatever is available, up to @p max_count.
 *
 * @param buf        Destination buffer.
 * @param max_count  Maximum samples to read.
 * @return           Number of samples actually read (0 if empty).
 */
size_t audio_mono_ringbuf_read(audio_mono_ringbuf_t *rb,
                               int16_t *buf, size_t max_count);

/**
 * @brief Return the number of mono samples currently available to read.
 */
size_t audio_mono_ringbuf_available(const audio_mono_ringbuf_t *rb);

/**
 * @brief Discard everything currently buffered (jump read_pos to write_pos).
 *
 * Capture/process run continuously in the background regardless of
 * recording state, so stale audio can sit in the buffer for as long as
 * nothing drains it. Call this right before a new recording starts
 * consuming the buffer, so it begins with live audio instead of
 * replaying whatever backlog (including the button-press click) had
 * accumulated while idle.
 */
void audio_mono_ringbuf_discard_available(audio_mono_ringbuf_t *rb);

/**
 * @brief Return and optionally reset the overflow counter.
 *
 * @param reset  If true, the counter is zeroed after reading.
 * @return       Number of samples dropped since last reset.
 */
uint32_t audio_mono_ringbuf_get_overflow(audio_mono_ringbuf_t *rb, bool reset);

/**
 * @brief Return the ring buffer's capacity in mono samples.
 */
size_t audio_mono_ringbuf_capacity(const audio_mono_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif
