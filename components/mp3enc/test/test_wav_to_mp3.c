/** @file test_wav_to_mp3.c
 * @brief Unity test for wav_to_mp3() — round-trips a small WAV through the
 * shine encoder and checks the output is a well-formed MP3 stream.
 */

#include "unity.h"
#include "wav_to_mp3.h"
#include "wav_writer.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define TEST_WAV "/tmp/echo_pocket_test.wav"
#define TEST_MP3 "/tmp/echo_pocket_test.mp3"

static void write_test_wav(uint32_t sample_rate, double seconds)
{
    wav_writer_t *w = wav_writer_open(TEST_WAV, 1, sample_rate, 16);
    TEST_ASSERT_NOT_NULL(w);

    uint32_t nsamples = (uint32_t)(sample_rate * seconds);
    int16_t buf[256];
    uint32_t written = 0;
    while (written < nsamples) {
        uint32_t chunk = nsamples - written;
        if (chunk > 256) chunk = 256;
        for (uint32_t i = 0; i < chunk; i++) {
            /* 440 Hz tone — non-silent so shine has real work to do. */
            double t = (double)(written + i) / sample_rate;
            buf[i] = (int16_t)(3000.0 * sin(2.0 * 3.14159265 * 440.0 * t));
        }
        wav_writer_write(w, buf, chunk);
        written += chunk;
    }

    TEST_ASSERT_TRUE(wav_writer_finalize(w));
    wav_writer_close(w);
}

void test_wav_to_mp3_produces_valid_mp3(void)
{
    write_test_wav(16000, 0.5);

    remove(TEST_MP3);
    TEST_ASSERT_TRUE(wav_to_mp3(TEST_WAV, TEST_MP3));

    FILE *fp = fopen(TEST_MP3, "rb");
    TEST_ASSERT_NOT_NULL(fp);

    uint8_t header[2];
    TEST_ASSERT_EQUAL_INT(2, fread(header, 1, 2, fp));

    /* MP3 frame sync: 11 set bits at the start of the first frame. */
    TEST_ASSERT_EQUAL_HEX8(0xFF, header[0]);
    TEST_ASSERT_EQUAL_HEX8(0xE0, header[1] & 0xE0);

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);

    TEST_ASSERT_GREATER_THAN(0, size);

    remove(TEST_WAV);
    remove(TEST_MP3);
}

/* Regression check: walk every frame header declared by the MPEG2/16kHz
 * stream and verify the next sync word actually lands where the header's
 * own bitrate/samplerate/padding fields say it should. A frame-size
 * miscalculation in the encoder (integer vs. float slot math, or a
 * per-granule bit budget overflow) desyncs every consecutive frame after
 * the first, which is silent corruption real decoders (and Telegram) trip
 * over as "0:00 duration" or worse. */
void test_wav_to_mp3_frames_stay_in_sync(void)
{
    static const int bitrates_mpeg2_l3[16] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
    };
    static const int samplerates_mpeg2[4] = { 22050, 24000, 16000, 0 };

    write_test_wav(16000, 3.0);

    remove(TEST_MP3);
    TEST_ASSERT_TRUE(wav_to_mp3(TEST_WAV, TEST_MP3));

    FILE *fp = fopen(TEST_MP3, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *data = malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_INT(size, fread(data, 1, (size_t)size, fp));
    fclose(fp);

    long pos = 0;
    int frame_count = 0;
    while (pos + 4 <= size) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, data[pos]);
        TEST_ASSERT_EQUAL_HEX8(0xE0, data[pos + 1] & 0xE0);

        int bitrate_idx = (data[pos + 2] >> 4) & 0xF;
        int sr_idx = (data[pos + 2] >> 2) & 0x3;
        int padding = (data[pos + 2] >> 1) & 0x1;
        int bitrate = bitrates_mpeg2_l3[bitrate_idx];
        int samplerate = samplerates_mpeg2[sr_idx];
        TEST_ASSERT_TRUE(bitrate > 0 && samplerate > 0);

        int framelen = 72 * bitrate * 1000 / samplerate + padding;
        pos += framelen;
        frame_count++;
    }

    /* Reached the end exactly (bar a final short/padded frame) instead of
     * bailing out mid-stream on a bad sync word. */
    TEST_ASSERT_GREATER_THAN(5, frame_count);

    free(data);
    remove(TEST_WAV);
    remove(TEST_MP3);
}

void test_wav_to_mp3_rejects_missing_file(void)
{
    TEST_ASSERT_FALSE(wav_to_mp3("/tmp/echo_pocket_does_not_exist.wav", TEST_MP3));
}

/* Shorter than the trimmed tail (150ms) — must keep the clip whole rather
 * than trimming it away to an empty (unusable) MP3. */
void test_wav_to_mp3_handles_clip_shorter_than_trim(void)
{
    write_test_wav(16000, 0.05);

    remove(TEST_MP3);
    TEST_ASSERT_TRUE(wav_to_mp3(TEST_WAV, TEST_MP3));

    FILE *fp = fopen(TEST_MP3, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);

    TEST_ASSERT_GREATER_THAN(0, size);

    remove(TEST_WAV);
    remove(TEST_MP3);
}
