/** @file test_ring_buffer.c
 * @brief Unity tests for the audio ring buffer (pure logic, no hardware).
 *
 * Covers:
 *   - Init/allocation/teardown
 *   - Basic write-then-read data integrity
 *   - Read from empty buffer returns 0
 *   - Write when near-full -> overflow, correct overflow count
 *   - Wrap-around (write past capacity boundary, read past boundary)
 *   - Concurrent read/write pattern (single-producer, single-consumer)
 *   - Downmix helper correctness
 */

#include "unity.h"
#include "audio_ringbuf.h"
#include "audio_mono_ringbuf.h"
#include <string.h>
#include <stdlib.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Number of int16_t samples per stereo frame. */
#define SPF  2

/** Fill a stereo buffer with a known pattern: frame[i] → { L=i*2, R=i*2+1 } */
static void fill_pattern(int16_t *buf, size_t frames, size_t base)
{
    for (size_t i = 0; i < frames; i++) {
        buf[i * 2]     = (int16_t)((base + i) * 2);
        buf[i * 2 + 1] = (int16_t)((base + i) * 2 + 1);
    }
}

/** Assert a stereo buffer matches the pattern: frame[i] → { L=base*2, R=base*2+1 } */
static void assert_pattern(const int16_t *buf, size_t frames, size_t base)
{
    for (size_t i = 0; i < frames; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(
            (int16_t)((base + i) * 2),
            buf[i * 2],
            "Left sample mismatch");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(
            (int16_t)((base + i) * 2 + 1),
            buf[i * 2 + 1],
            "Right sample mismatch");
    }
}

/* ── Init / teardown ─────────────────────────────────────────────────── */

void test_rb_alloc_free(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_size_t(100, audio_ringbuf_capacity(rb));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(rb));
    TEST_ASSERT_EQUAL_UINT32(0, audio_ringbuf_get_overflow(rb, false));
    audio_ringbuf_free(rb);
}

void test_rb_alloc_zero_returns_null(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(0);
    TEST_ASSERT_NULL(rb);
}

void test_rb_free_null_is_safe(void)
{
    audio_ringbuf_free(NULL);  /* must not crash */
}

void test_rb_capacity_null_safe(void)
{
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_capacity(NULL));
}

void test_rb_available_null_safe(void)
{
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(NULL));
}

void test_rb_overflow_null_safe(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, audio_ringbuf_get_overflow(NULL, false));
}

/* ── Basic write / read ──────────────────────────────────────────────── */

void test_rb_write_read_roundtrip(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t src[20 * SPF];
    fill_pattern(src, 20, 0);

    size_t written = audio_ringbuf_write(rb, src, 20);
    TEST_ASSERT_EQUAL_size_t(20, written);
    TEST_ASSERT_EQUAL_size_t(20, audio_ringbuf_available(rb));

    int16_t dst[20 * SPF];
    memset(dst, 0xCD, sizeof(dst));
    size_t read = audio_ringbuf_read(rb, dst, 20);
    TEST_ASSERT_EQUAL_size_t(20, read);
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(rb));

    assert_pattern(dst, 20, 0);

    audio_ringbuf_free(rb);
}

void test_rb_read_empty_returns_zero(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t dst[4 * SPF];
    size_t read = audio_ringbuf_read(rb, dst, 4);
    TEST_ASSERT_EQUAL_size_t(0, read);

    audio_ringbuf_free(rb);
}

void test_rb_read_less_than_available(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t src[30 * SPF];
    fill_pattern(src, 30, 0);
    audio_ringbuf_write(rb, src, 30);
    TEST_ASSERT_EQUAL_size_t(30, audio_ringbuf_available(rb));

    int16_t dst[10 * SPF];
    size_t read = audio_ringbuf_read(rb, dst, 10);
    TEST_ASSERT_EQUAL_size_t(10, read);
    TEST_ASSERT_EQUAL_size_t(20, audio_ringbuf_available(rb));
    assert_pattern(dst, 10, 0);

    audio_ringbuf_free(rb);
}

void test_rb_write_null_params(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_write(NULL, NULL, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_write(rb, NULL, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_write(NULL, (int16_t *)1, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_write(rb, (int16_t *)1, 0));

    audio_ringbuf_free(rb);
}

void test_rb_read_null_params(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_read(NULL, NULL, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_read(rb, NULL, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_read(NULL, (int16_t *)1, 10));
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_read(rb, (int16_t *)1, 0));

    audio_ringbuf_free(rb);
}

/* ── Overflow ────────────────────────────────────────────────────────── */

void test_rb_overflow_drops_oldest(void)
{
    /* Small buffer: 10 frames capacity                                   */
    audio_ringbuf_t *rb = audio_ringbuf_alloc(10);
    TEST_ASSERT_NOT_NULL(rb);

    /* Write 7 frames first so there's some data to drop on overflow     */
    int16_t src[15 * SPF];
    fill_pattern(src, 15, 0);
    audio_ringbuf_write(rb, src, 7);
    TEST_ASSERT_EQUAL_size_t(7, audio_ringbuf_available(rb));

    /* Now write 8 more — 7 + 8 = 15 > 10, so 5 frames must be dropped   */
    audio_ringbuf_write(rb, src + (7 * SPF), 8);
    TEST_ASSERT_EQUAL_UINT32(5, audio_ringbuf_get_overflow(rb, false));

    /* After overflow, buffer should hold exactly 10 frames (capacity).
     * The oldest 5 frames were dropped, so remaining data starts at
     * frame 5 (values 10/11...).                                         */
    TEST_ASSERT_EQUAL_size_t(10, audio_ringbuf_available(rb));

    int16_t dst[10 * SPF];
    memset(dst, 0xCD, sizeof(dst));
    size_t read = audio_ringbuf_read(rb, dst, 10);
    TEST_ASSERT_EQUAL_size_t(10, read);
    assert_pattern(dst, 10, 5);  /* starts at frame 5 */

    audio_ringbuf_free(rb);
}

void test_rb_overflow_reset_counter(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(5);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t src[10 * SPF];
    fill_pattern(src, 10, 0);

    audio_ringbuf_write(rb, src, 10);          /* 5 dropped */
    audio_ringbuf_write(rb, src, 10);          /* 5 more dropped */

    TEST_ASSERT_EQUAL_UINT32(10, audio_ringbuf_get_overflow(rb, false));
    TEST_ASSERT_EQUAL_UINT32(10, audio_ringbuf_get_overflow(rb, true));  /* read + reset */
    TEST_ASSERT_EQUAL_UINT32(0, audio_ringbuf_get_overflow(rb, false));  /* now zero */

    audio_ringbuf_free(rb);
}

/* ── Wrap-around ─────────────────────────────────────────────────────── */

void test_rb_wrap_write_across_boundary(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    /* Fill 95 frames, then read 90 back — leaving 5 frames at the end   */
    int16_t src[100 * SPF];
    fill_pattern(src, 100, 0);
    audio_ringbuf_write(rb, src, 95);

    int16_t tmp[90 * SPF];
    audio_ringbuf_read(rb, tmp, 90);
    TEST_ASSERT_EQUAL_size_t(5, audio_ringbuf_available(rb));

    /* Now write 10 more frames.  5 go at the end, 5 wrap to the start.  */
    fill_pattern(src, 10, 95);
    audio_ringbuf_write(rb, src, 10);
    TEST_ASSERT_EQUAL_size_t(15, audio_ringbuf_available(rb));

    /* Read all 15 — should be frames 5..19 (5 original + 10 new)        */
    int16_t dst[15 * SPF];
    audio_ringbuf_read(rb, dst, 15);
    assert_pattern(dst, 5, 5);   /* original 5: frames 5..9              */
    assert_pattern(dst + (5 * SPF), 10, 95); /* new 10: frames 95..104 → wraparound */

    audio_ringbuf_free(rb);
}

void test_rb_wrap_read_across_boundary(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    /* Same setup: write 95, read 90 → 5 left at tail, then write 10     */
    int16_t src[100 * SPF];
    fill_pattern(src, 100, 0);
    audio_ringbuf_write(rb, src, 95);

    int16_t tmp[90 * SPF];
    audio_ringbuf_read(rb, tmp, 90);

    fill_pattern(src, 10, 95);
    audio_ringbuf_write(rb, src, 10);

    /* Read all 15 in one call — this requires a wrap read internally     */
    int16_t dst[15 * SPF];
    memset(dst, 0xCD, sizeof(dst));
    audio_ringbuf_read(rb, dst, 15);

    /* Verify the data wraps correctly                                  */
    assert_pattern(dst, 5, 5);    /* tail: frames 5..9                   */
    assert_pattern(dst + (5 * SPF), 10, 95); /* head: frames 95..104     */

    audio_ringbuf_free(rb);
}

void test_rb_wrap_write_exactly_to_end(void)
{
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t src[105 * SPF];
    fill_pattern(src, 105, 0);

    /* Write 98, read 98 → buffer empty, write pos at 98                 */
    audio_ringbuf_write(rb, src, 98);
    int16_t tmp[98 * SPF];
    audio_ringbuf_read(rb, tmp, 98);
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(rb));

    /* Now write exactly 2 frames to fill to position 100 (= 0 % 100).
     * Then read them back — the read should wrap from 98 to 0 correctly. */
    audio_ringbuf_write(rb, src + (98 * SPF), 2);
    int16_t dst[2 * SPF];
    audio_ringbuf_read(rb, dst, 2);
    assert_pattern(dst, 2, 98);

    audio_ringbuf_free(rb);
}

/* ── Producer-consumer pattern ───────────────────────────────────────── */

void test_rb_producer_consumer_interleaved(void)
{
    /* Simulates the real capture-to-AFE flow: producer writes chunks,
     * consumer reads chunks, interleaved, with neither side blocking.    */
    audio_ringbuf_t *rb = audio_ringbuf_alloc(100);
    TEST_ASSERT_NOT_NULL(rb);

    size_t prod_idx = 0;
    size_t cons_idx = 0;
    int16_t src[200 * SPF];
    fill_pattern(src, 200, 0);

    /* 10 iterations of: write 8, read 5                                 */
    for (int iter = 0; iter < 10; iter++) {
        audio_ringbuf_write(rb, src + (prod_idx * SPF), 8);
        prod_idx += 8;

        int16_t dst[5 * SPF];
        size_t read = audio_ringbuf_read(rb, dst, 5);
        TEST_ASSERT_EQUAL_size_t(5, read);
        assert_pattern(dst, 5, cons_idx);
        cons_idx += 5;
    }

    /* After 10 iterations: written 80, read 50 → 30 available            */
    TEST_ASSERT_EQUAL_size_t(30, audio_ringbuf_available(rb));

    /* Drain remaining 30                                                 */
    int16_t dst[30 * SPF];
    audio_ringbuf_read(rb, dst, 30);
    assert_pattern(dst, 30, cons_idx);
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(rb));

    audio_ringbuf_free(rb);
}

void test_rb_producer_faster_than_consumer(void)
{
    /* Producer writes much faster than consumer reads → overflow.
     * Consumer eventually catches up and reads what remains.              */
    audio_ringbuf_t *rb = audio_ringbuf_alloc(20);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t src[100 * SPF];
    fill_pattern(src, 100, 0);

    /* Write 50 frames (30 overflow) → consumer reads 10 → write 30 more…
     * The consumer should always get the most recent contiguous data.    */
    audio_ringbuf_write(rb, src, 50);  /* 30 overflow, 20 remain */
    TEST_ASSERT_EQUAL_UINT32(30, audio_ringbuf_get_overflow(rb, false));

    int16_t dst[10 * SPF];
    audio_ringbuf_read(rb, dst, 10);   /* read 10, 10 remain */

    audio_ringbuf_write(rb, src + (50 * SPF), 15); /* 5 overflow, 20 remain */
    TEST_ASSERT_EQUAL_UINT32(35, audio_ringbuf_get_overflow(rb, false));

    /* Drain what's left — should be the most recent 20 frames           */
    int16_t final[20 * SPF];
    audio_ringbuf_read(rb, final, 20);
    TEST_ASSERT_EQUAL_size_t(0, audio_ringbuf_available(rb));

    /* Write sequence: src 0..49, read 10 (getting 30..39? no — overflow
     * drops the oldest, so after first write of 50 into cap 20, we
     * keep frames 30..49.  Read 10 takes 30..39, leaving 40..49.
     * Second write of 15 at frames 50..64: total 10+15=25 > 20, drop 5
     * oldest (40..44), keep 45..64.  Final read should get 45..64.
     *
     * 45 × 2 = 90 for L channel, 91 for R channel.                      */
    assert_pattern(final, 20, 45);

    audio_ringbuf_free(rb);
}

/* ── Downmix helper ──────────────────────────────────────────────────── */

void test_downmix_silent(void)
{
    int16_t stereo[4 * SPF] = {0};
    int16_t mono[4];
    audio_downmix_2ch_to_mono(stereo, mono, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT16(0, mono[i]);
    }
}

void test_downmix_identical_channels(void)
{
    /* Both channels carry the same value → mono should equal it          */
    int16_t stereo[4 * SPF];
    for (int i = 0; i < 4; i++) {
        stereo[i * 2]     = 100;
        stereo[i * 2 + 1] = 100;
    }

    int16_t mono[4];
    audio_downmix_2ch_to_mono(stereo, mono, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT16(100, mono[i]);
    }
}

void test_downmix_opposite_channels(void)
{
    /* L = +200, R = -200 → average = 0                                  */
    int16_t stereo[2 * SPF];
    stereo[0] =  200;
    stereo[1] = -200;
    stereo[2] =  -50;
    stereo[3] =   50;

    int16_t mono[2];
    audio_downmix_2ch_to_mono(stereo, mono, 2);
    TEST_ASSERT_EQUAL_INT16(0, mono[0]);
    TEST_ASSERT_EQUAL_INT16(0, mono[1]);
}

void test_downmix_rounds_toward_zero(void)
{
    /* L = 7, R = 8 → (7+8)/2 = 7 (integer division truncates toward zero)*/
    int16_t stereo[1 * SPF] = {7, 8};
    int16_t mono[1];
    audio_downmix_2ch_to_mono(stereo, mono, 1);
    TEST_ASSERT_EQUAL_INT16(7, mono[0]);
}

void test_downmix_null_safety(void)
{
    /* Must not crash with NULL pointers                                  */
    int16_t buf[4 * SPF];
    int16_t out[4];
    audio_downmix_2ch_to_mono(NULL, out, 4);
    audio_downmix_2ch_to_mono(buf, NULL, 4);
    audio_downmix_2ch_to_mono(buf, out, 0);
    /* No assertions — just confirming no crash                          */
}

void test_downmix_large_values(void)
{
    /* Near full-scale: 32767 + 32767 = 65534 / 2 = 32767 (safe)         */
    int16_t stereo[1 * SPF] = {32767, 32767};
    int16_t mono[1];
    audio_downmix_2ch_to_mono(stereo, mono, 1);
    TEST_ASSERT_EQUAL_INT16(32767, mono[0]);
}

/* ── Mono ring buffer: discard_available ────────────────────────────── */

void test_mono_rb_discard_drops_backlog(void)
{
    /* Simulates the always-on capture pipeline accumulating a backlog
     * while idle: discard must make it unreadable without touching
     * anything written afterward (the live audio the recording should
     * actually start with). */
    audio_mono_ringbuf_t *rb = audio_mono_ringbuf_alloc(16);
    TEST_ASSERT_NOT_NULL(rb);

    int16_t stale[4] = {1, 2, 3, 4};
    audio_mono_ringbuf_write(rb, stale, 4);
    TEST_ASSERT_EQUAL_size_t(4, audio_mono_ringbuf_available(rb));

    audio_mono_ringbuf_discard_available(rb);
    TEST_ASSERT_EQUAL_size_t(0, audio_mono_ringbuf_available(rb));

    int16_t live[2] = {9, 10};
    audio_mono_ringbuf_write(rb, live, 2);

    int16_t out[2] = {0};
    size_t got = audio_mono_ringbuf_read(rb, out, 2);
    TEST_ASSERT_EQUAL_size_t(2, got);
    TEST_ASSERT_EQUAL_INT16(9, out[0]);
    TEST_ASSERT_EQUAL_INT16(10, out[1]);

    audio_mono_ringbuf_free(rb);
}

void test_mono_rb_discard_null_safe(void)
{
    audio_mono_ringbuf_discard_available(NULL);  /* must not crash */
}

void test_mono_rb_discard_on_empty_is_noop(void)
{
    audio_mono_ringbuf_t *rb = audio_mono_ringbuf_alloc(8);
    TEST_ASSERT_NOT_NULL(rb);
    audio_mono_ringbuf_discard_available(rb);
    TEST_ASSERT_EQUAL_size_t(0, audio_mono_ringbuf_available(rb));
    audio_mono_ringbuf_free(rb);
}
