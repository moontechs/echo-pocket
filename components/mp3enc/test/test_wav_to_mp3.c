/** @file test_wav_to_mp3.c
 * @brief Unity test for wav_to_mp3() — round-trips a small WAV through the
 * shine encoder and checks the output is a well-formed MP3 stream.
 */

#include "unity.h"
#include "wav_to_mp3.h"
#include "wav_writer.h"

#include <stdio.h>
#include <stdint.h>
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

void test_wav_to_mp3_rejects_missing_file(void)
{
    TEST_ASSERT_FALSE(wav_to_mp3("/tmp/echo_pocket_does_not_exist.wav", TEST_MP3));
}
