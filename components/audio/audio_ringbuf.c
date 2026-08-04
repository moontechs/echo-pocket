/** @file audio_ringbuf.c
 * @brief Pure-C ring buffer implementation for I2S audio frames.
 *
 * No ESP-IDF dependencies — runs in logic_tests without hardware.
 * The ring buffer itself is position-independent; the caller
 * (audio_capture.c) is responsible for placing the sample storage
 * in PSRAM via audio_ringbuf_alloc().
 */

#include "audio_ringbuf.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Number of int16_t samples per stereo frame. */
#define SAMPLES_PER_FRAME  2

/**
 * Space (in frames) between the write cursor and the read cursor.
 * read_pos always lags write_pos; their difference is the occupied range.
 */
static inline size_t frames_available(const audio_ringbuf_t *rb)
{
    return rb->write_pos - rb->read_pos;
}

/**
 * Free space (in frames) remaining before the buffer is full.
 */
static inline size_t frames_free(const audio_ringbuf_t *rb)
{
    return rb->capacity_frames - frames_available(rb);
}

/* ── Alloc / free ────────────────────────────────────────────────────── */

audio_ringbuf_t *audio_ringbuf_alloc(size_t capacity_frames)
{
    if (capacity_frames == 0) {
        return NULL;
    }

    audio_ringbuf_t *rb = calloc(1, sizeof(*rb));
    if (!rb) {
        return NULL;
    }

    /* Sample buffer is in PSRAM when called from firmware (heap_caps_malloc
     * in audio_capture.c replaces this allocation after init).  For unit
     * tests we use regular calloc so tests run without PSRAM. */
    rb->buffer = calloc(capacity_frames, SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    rb->capacity_frames = capacity_frames;
    rb->write_pos = 0;
    rb->read_pos  = 0;
    rb->overflow_count = 0;

    return rb;
}

void audio_ringbuf_free(audio_ringbuf_t *rb)
{
    if (!rb) return;
    free(rb->buffer);
    rb->buffer = NULL;
    free(rb);
}

/* ── Write ───────────────────────────────────────────────────────────── */

size_t audio_ringbuf_write(audio_ringbuf_t *rb, const int16_t *data, size_t frames)
{
    if (!rb || !rb->buffer || !data || frames == 0) {
        return 0;
    }

    size_t cap  = rb->capacity_frames;
    size_t free_sp = frames_free(rb);

    /* If the write would overflow, advance the read cursor to drop the
     * oldest frames.  Capture never blocks. */
    if (frames > free_sp) {
        size_t drop = frames - free_sp;
        rb->read_pos += drop;
        rb->overflow_count += (uint32_t)drop;
    }

    /* Write data — may wrap around the buffer end so we handle two segments */
    size_t w = rb->write_pos % cap;
    size_t samples = frames * SAMPLES_PER_FRAME;

    if (w + samples <= cap * SAMPLES_PER_FRAME) {
        /* Single contiguous write */
        memcpy(&rb->buffer[w], data, samples * sizeof(int16_t));
    } else {
        /* Two-part write around the wrap point */
        size_t first_part_samples = (cap * SAMPLES_PER_FRAME) - w;
        memcpy(&rb->buffer[w], data, first_part_samples * sizeof(int16_t));
        memcpy(rb->buffer,
               data + first_part_samples,
               (samples - first_part_samples) * sizeof(int16_t));
    }

    rb->write_pos += frames;
    return frames;
}

/* ── Read ────────────────────────────────────────────────────────────── */

size_t audio_ringbuf_read(audio_ringbuf_t *rb, int16_t *buf, size_t max_frames)
{
    if (!rb || !rb->buffer || !buf || max_frames == 0) {
        return 0;
    }

    size_t avail = frames_available(rb);
    size_t to_read = (avail < max_frames) ? avail : max_frames;
    if (to_read == 0) {
        return 0;
    }

    size_t cap  = rb->capacity_frames;
    size_t r    = rb->read_pos % cap;
    size_t samples = to_read * SAMPLES_PER_FRAME;

    if (r + samples <= cap * SAMPLES_PER_FRAME) {
        /* Single contiguous read */
        memcpy(buf, &rb->buffer[r], samples * sizeof(int16_t));
    } else {
        /* Two-part read around the wrap point */
        size_t first_part_samples = (cap * SAMPLES_PER_FRAME) - r;
        memcpy(buf, &rb->buffer[r], first_part_samples * sizeof(int16_t));
        memcpy(buf + first_part_samples,
               rb->buffer,
               (samples - first_part_samples) * sizeof(int16_t));
    }

    rb->read_pos += to_read;
    return to_read;
}

/* ── Query ───────────────────────────────────────────────────────────── */

size_t audio_ringbuf_available(const audio_ringbuf_t *rb)
{
    if (!rb) return 0;
    return frames_available(rb);
}

uint32_t audio_ringbuf_get_overflow(audio_ringbuf_t *rb, bool reset)
{
    if (!rb) return 0;
    uint32_t val = rb->overflow_count;
    if (reset) {
        rb->overflow_count = 0;
    }
    return val;
}

size_t audio_ringbuf_capacity(const audio_ringbuf_t *rb)
{
    if (!rb) return 0;
    return rb->capacity_frames;
}

/* ── Downmix helper ──────────────────────────────────────────────────── */

void audio_downmix_2ch_to_mono(const int16_t *stereo, int16_t *mono, size_t frames)
{
    if (!stereo || !mono || frames == 0) return;

    for (size_t i = 0; i < frames; i++) {
        /* Average the two channels to avoid clipping */
        int32_t sum = (int32_t)stereo[i * 2] + (int32_t)stereo[i * 2 + 1];
        mono[i] = (int16_t)(sum / 2);
    }
}
