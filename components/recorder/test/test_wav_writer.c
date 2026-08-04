/** @file test_wav_writer.c
 * @brief Unity tests for WAV header math (pure logic, no file I/O).
 *
 * Covers:
 *   - wav_header_fill: correct magic bytes, computed sizes
 *   - Mono 16-bit 16kHz with various data sizes (0, small, large, max)
 *   - Stereo 16-bit 44.1kHz (non-default config, verifies byte_rate/block_align)
 *   - Header size constant matches struct size
 *   - Null pointer safety
 */

#include "unity.h"
#include "wav_writer.h"
#include "recorder_split.h"
#include <string.h>
#include <stddef.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Verify magic bytes in a filled header. */
static void assert_magic(const wav_header_t *hdr)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE("RIFF", hdr->riff_id, 4, "RIFF magic");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE("WAVE", hdr->wave_id, 4, "WAVE magic");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE("fmt ", hdr->fmt_id,  4, "fmt  magic");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE("data", hdr->data_id, 4, "data magic");
}

/* ── Mono 16kHz 16-bit ───────────────────────────────────────────────── */

void test_wav_header_mono_16k_zero_data(void)
{
    wav_header_t hdr;
    memset(&hdr, 0xAA, sizeof(hdr));  /* poison to catch uninitialized fields */

    wav_header_fill(&hdr, 1, 16000, 16, 0);

    assert_magic(&hdr);

    TEST_ASSERT_EQUAL_UINT16(1, hdr.audio_format);         /* PCM            */
    TEST_ASSERT_EQUAL_UINT16(1, hdr.num_channels);         /* mono           */
    TEST_ASSERT_EQUAL_UINT32(16000, hdr.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(16, hdr.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.block_align);          /* 1ch × 2 bytes  */
    TEST_ASSERT_EQUAL_UINT32(32000, hdr.byte_rate);        /* 16000 × 2      */
    TEST_ASSERT_EQUAL_UINT32(16, hdr.fmt_size);            /* PCM            */
    TEST_ASSERT_EQUAL_UINT32(0, hdr.data_size);

    /* file_size = 4 ("WAVE") + (8 + 16) + (8 + 0) = 36                    */
    TEST_ASSERT_EQUAL_UINT32(36, hdr.file_size);
}

void test_wav_header_mono_16k_small_data(void)
{
    wav_header_t hdr;
    wav_header_fill(&hdr, 1, 16000, 16, 1000);

    assert_magic(&hdr);
    TEST_ASSERT_EQUAL_UINT32(1000, hdr.data_size);
    /* file_size = 4 + 24 + 8 + 1000 = 1036 */
    TEST_ASSERT_EQUAL_UINT32(1036, hdr.file_size);
}

void test_wav_header_mono_16k_10min_data(void)
{
    /* 10 minutes of mono 16kHz 16-bit:
     *   10 × 60 × 16000 × 2 = 19,200,000 bytes                        */
    uint32_t data_size = 10 * 60 * 16000 * 2;

    wav_header_t hdr;
    wav_header_fill(&hdr, 1, 16000, 16, data_size);

    assert_magic(&hdr);
    TEST_ASSERT_EQUAL_UINT32(data_size, hdr.data_size);
    /* file_size = 4 + 24 + 8 + data_size */
    TEST_ASSERT_EQUAL_UINT32(36 + data_size, hdr.file_size);
    TEST_ASSERT_EQUAL_UINT16(1, hdr.num_channels);
    TEST_ASSERT_EQUAL_UINT32(16000, hdr.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(16, hdr.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT32(32000, hdr.byte_rate);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.block_align);
}

void test_wav_header_mono_16k_max_data(void)
{
    /* Maximum uint32_t data size: 0xFFFFFFFF                             */
    uint32_t data_size = 0xFFFFFFFFu;

    wav_header_t hdr;
    wav_header_fill(&hdr, 1, 16000, 16, data_size);

    assert_magic(&hdr);
    TEST_ASSERT_EQUAL_UINT32(data_size, hdr.data_size);

    /* file_size wraps around — this is the WAV format limitation.
     * We just verify no crash and fields are set.                        */
}

/* ── Stereo 44.1kHz 16-bit (non-default config) ──────────────────────── */

void test_wav_header_stereo_44k(void)
{
    wav_header_t hdr;
    memset(&hdr, 0xAA, sizeof(hdr));

    wav_header_fill(&hdr, 2, 44100, 16, 88200); /* 1 second of stereo */

    assert_magic(&hdr);
    TEST_ASSERT_EQUAL_UINT16(1, hdr.audio_format);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.num_channels);
    TEST_ASSERT_EQUAL_UINT32(44100, hdr.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(16, hdr.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT16(4, hdr.block_align);     /* 2ch × 2 bytes   */
    TEST_ASSERT_EQUAL_UINT32(176400, hdr.byte_rate);  /* 44100 × 4       */
    TEST_ASSERT_EQUAL_UINT32(88200, hdr.data_size);
    TEST_ASSERT_EQUAL_UINT32(36 + 88200, hdr.file_size);
}

/* ── 24-bit, 48kHz stereo (edge case) ────────────────────────────────── */

void test_wav_header_stereo_48k_24bit(void)
{
    wav_header_t hdr;
    wav_header_fill(&hdr, 2, 48000, 24, 0);

    assert_magic(&hdr);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.num_channels);
    TEST_ASSERT_EQUAL_UINT32(48000, hdr.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(24, hdr.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT16(6, hdr.block_align);      /* 2ch × 3 bytes  */
    TEST_ASSERT_EQUAL_UINT32(288000, hdr.byte_rate);   /* 48000 × 6      */
}

/* ── Header size constant ────────────────────────────────────────────── */

void test_wav_header_size(void)
{
    /* WAV_HEADER_SIZE must match sizeof(wav_header_t) */
    TEST_ASSERT_EQUAL_size_t(sizeof(wav_header_t), WAV_HEADER_SIZE);

    /* Standard WAV header is 44 bytes */
    TEST_ASSERT_EQUAL_size_t(44, sizeof(wav_header_t));
}

/* ── Struct field offsets (basic sanity) ─────────────────────────────── */

void test_wav_header_offsets(void)
{
    /* Verify RIFF/data ID fields are at expected offsets for seek+patch */
    wav_header_t hdr;
    TEST_ASSERT_EQUAL_size_t(0,  offsetof(wav_header_t, riff_id));
    TEST_ASSERT_EQUAL_size_t(4,  offsetof(wav_header_t, file_size));
    TEST_ASSERT_EQUAL_size_t(8,  offsetof(wav_header_t, wave_id));
    TEST_ASSERT_EQUAL_size_t(40, offsetof(wav_header_t, data_size));

    /* data_id is at offset 36 */
    TEST_ASSERT_EQUAL_size_t(36, offsetof(wav_header_t, data_id));
}

/* ── Null pointer safety ────────────────────────────────────────────── */

void test_wav_header_fill_null(void)
{
    /* Must not crash with NULL header pointer */
    wav_header_fill(NULL, 1, 16000, 16, 0);
    /* No assertion — just confirming no crash */
}

/* ── Auto-split threshold (Task 7 state machine) ────────────────────── */

void test_recorder_split_below_threshold(void)
{
    /* 0 bytes — should not split */
    TEST_ASSERT_FALSE(recorder_should_split(0));

    /* 1 byte below threshold */
    uint32_t just_below = RECORDER_SPLIT_BYTES - 1;
    if (just_below < RECORDER_SPLIT_BYTES) { /* overflow guard for 0 case */
        TEST_ASSERT_FALSE(recorder_should_split(just_below));
    }

    /* Half the threshold */
    TEST_ASSERT_FALSE(recorder_should_split(RECORDER_SPLIT_BYTES / 2));

    /* 10 MB — well below the ~34.8 MB threshold */
    TEST_ASSERT_FALSE(recorder_should_split(10 * 1024 * 1024));
}

void test_recorder_split_at_threshold(void)
{
    /* Exactly at threshold → should split */
    TEST_ASSERT_TRUE(recorder_should_split(RECORDER_SPLIT_BYTES));
}

void test_recorder_split_above_threshold(void)
{
    /* Just above threshold */
    TEST_ASSERT_TRUE(recorder_should_split(RECORDER_SPLIT_BYTES + 1));

    /* Well above threshold */
    TEST_ASSERT_TRUE(recorder_should_split(RECORDER_SPLIT_BYTES + 1024 * 1024));

    /* Near uint32_t max — should still split */
    TEST_ASSERT_TRUE(recorder_should_split(0xFFFFFFFEu));
}

void test_recorder_split_threshold_matches_19_minutes(void)
{
    /* Verify RECORDER_SPLIT_BYTES = 19 × 60 × 16000 × 2 */
    uint32_t expected = (uint32_t)(19 * 60 * 16000 * sizeof(int16_t));
    TEST_ASSERT_EQUAL_UINT32(expected, RECORDER_SPLIT_BYTES);
}
