/** @file audio_mono_ringbuf.c
 * @brief Pure-C mono ring buffer implementation for processed AFE output.
 *
 * No ESP-IDF dependencies — runs in logic_tests without hardware.
 * Allocation uses calloc so tests run without PSRAM; the firmware
 * caller is responsible for placing the buffer in PSRAM via
 * heap_caps_calloc if needed.
 */

#include "audio_mono_ringbuf.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

static inline size_t samples_available(const audio_mono_ringbuf_t *rb)
{
    return rb->write_pos - rb->read_pos;
}

static inline size_t samples_free(const audio_mono_ringbuf_t *rb)
{
    return rb->capacity_samples - samples_available(rb);
}

/* ── Alloc / free ────────────────────────────────────────────────────── */

audio_mono_ringbuf_t *audio_mono_ringbuf_alloc(size_t capacity_samples)
{
    if (capacity_samples == 0) {
        return NULL;
    }

    audio_mono_ringbuf_t *rb = calloc(1, sizeof(*rb));
    if (!rb) {
        return NULL;
    }

    rb->buffer = calloc(capacity_samples, sizeof(int16_t));
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    rb->capacity_samples = capacity_samples;
    rb->write_pos = 0;
    rb->read_pos  = 0;
    rb->overflow_count = 0;

    return rb;
}

void audio_mono_ringbuf_free(audio_mono_ringbuf_t *rb)
{
    if (!rb) return;
    free(rb->buffer);
    rb->buffer = NULL;
    free(rb);
}

/* ── Write ───────────────────────────────────────────────────────────── */

size_t audio_mono_ringbuf_write(audio_mono_ringbuf_t *rb,
                                const int16_t *data, size_t count)
{
    if (!rb || !rb->buffer || !data || count == 0) {
        return 0;
    }

    size_t cap  = rb->capacity_samples;
    size_t free_sp = samples_free(rb);

    /* If the write would overflow, advance the read cursor to drop the
     * oldest samples.  The process task never blocks. */
    if (count > free_sp) {
        size_t drop = count - free_sp;
        rb->read_pos += drop;
        rb->overflow_count += (uint32_t)drop;
    }

    /* Write data — may wrap around the buffer end */
    size_t w = rb->write_pos % cap;

    if (w + count <= cap) {
        /* Single contiguous write */
        memcpy(&rb->buffer[w], data, count * sizeof(int16_t));
    } else {
        /* Two-part write around the wrap point */
        size_t first_part = cap - w;
        memcpy(&rb->buffer[w], data, first_part * sizeof(int16_t));
        memcpy(rb->buffer, data + first_part,
               (count - first_part) * sizeof(int16_t));
    }

    rb->write_pos += count;
    return count;
}

/* ── Read ────────────────────────────────────────────────────────────── */

size_t audio_mono_ringbuf_read(audio_mono_ringbuf_t *rb,
                               int16_t *buf, size_t max_count)
{
    if (!rb || !rb->buffer || !buf || max_count == 0) {
        return 0;
    }

    size_t avail = samples_available(rb);
    size_t to_read = (avail < max_count) ? avail : max_count;
    if (to_read == 0) {
        return 0;
    }

    size_t cap = rb->capacity_samples;
    size_t r   = rb->read_pos % cap;

    if (r + to_read <= cap) {
        /* Single contiguous read */
        memcpy(buf, &rb->buffer[r], to_read * sizeof(int16_t));
    } else {
        /* Two-part read around the wrap point */
        size_t first_part = cap - r;
        memcpy(buf, &rb->buffer[r], first_part * sizeof(int16_t));
        memcpy(buf + first_part, rb->buffer,
               (to_read - first_part) * sizeof(int16_t));
    }

    rb->read_pos += to_read;
    return to_read;
}

/* ── Query ───────────────────────────────────────────────────────────── */

size_t audio_mono_ringbuf_available(const audio_mono_ringbuf_t *rb)
{
    if (!rb) return 0;
    return samples_available(rb);
}

uint32_t audio_mono_ringbuf_get_overflow(audio_mono_ringbuf_t *rb, bool reset)
{
    if (!rb) return 0;
    uint32_t val = rb->overflow_count;
    if (reset) {
        rb->overflow_count = 0;
    }
    return val;
}

size_t audio_mono_ringbuf_capacity(const audio_mono_ringbuf_t *rb)
{
    if (!rb) return 0;
    return rb->capacity_samples;
}
