/** @file test_process_pipeline.c
 * @brief Unity tests for voice level computation and smoothing logic.
 *
 * Tests the pure functions in voice_level.h — RMS computation, EMA
 * smoothing, and dBFS conversion.  The AFE model itself is not tested
 * here (that's an on-device manual check).
 *
 * All tests are pure logic — no hardware, no ESP-IDF, no FreeRTOS.
 */

#include "unity.h"
#include "voice_level.h"
#include <math.h>
#include <string.h>

/* ── RMS computation ─────────────────────────────────────────────────── */

/** Silence → RMS = 0.0 */
void test_voice_rms_silence(void)
{
    int16_t samples[256];
    memset(samples, 0, sizeof(samples));

    float level = voice_level_compute_rms(samples, 256);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, level);
}

/** Full-scale DC → RMS = 1.0 */
void test_voice_rms_full_scale_dc(void)
{
    int16_t samples[256];
    for (int i = 0; i < 256; i++) {
        samples[i] = 32767;
    }

    float level = voice_level_compute_rms(samples, 256);
    /* sqrt(32767²) / 32767 = 1.0 (with tiny float rounding) */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, level);
}

/** Half-scale square wave → RMS ≈ 0.5 */
void test_voice_rms_half_scale(void)
{
    int16_t samples[256];
    for (int i = 0; i < 256; i++) {
        samples[i] = (i % 2 == 0) ? 16384 : -16384;
    }

    float level = voice_level_compute_rms(samples, 256);
    /* RMS of ±16384 = 16384/32767 ≈ 0.5 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, level);
}

/** Single-sample full-scale → RMS = 1.0 */
void test_voice_rms_single_sample(void)
{
    int16_t samples[1] = { 32767 };
    float level = voice_level_compute_rms(samples, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, level);
}

/** Single zero sample → RMS = 0.0 */
void test_voice_rms_single_zero(void)
{
    int16_t samples[1] = { 0 };
    float level = voice_level_compute_rms(samples, 1);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, level);
}

/** Count = 0 → returns 0.0 */
void test_voice_rms_zero_count(void)
{
    int16_t dummy = 0;
    float level = voice_level_compute_rms(&dummy, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, level);
}

/** NULL samples → returns 0.0 */
void test_voice_rms_null_samples(void)
{
    float level = voice_level_compute_rms(NULL, 256);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, level);
}

/** 256-sample sine wave at half amplitude → RMS = 0.5 */
void test_voice_rms_sine_wave(void)
{
    int16_t samples[256];
    for (int i = 0; i < 256; i++) {
        /* sin from 0 to 2π across 256 samples */
        float phase = 2.0f * 3.14159265f * (float)i / 256.0f;
        samples[i] = (int16_t)(16384.0f * sinf(phase));
    }

    float level = voice_level_compute_rms(samples, 256);
    /* RMS of a sine wave with amplitude A = A/sqrt(2)/32767
     * For A=16384: 16384/1.4142/32767 ≈ 0.3535 */
    float expected = (16384.0f / 1.41421356f) / 32767.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.02f, expected, level);
}

/** Large chunk (16k samples) — validates int64_t sum doesn't overflow. */
void test_voice_rms_large_chunk(void)
{
    const size_t N = 16000;
    int16_t *samples = calloc(N, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(samples);

    /* Fill with alternating ±16384 */
    for (size_t i = 0; i < N; i++) {
        samples[i] = (i % 2 == 0) ? 16384 : -16384;
    }

    float level = voice_level_compute_rms(samples, N);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, level);

    free(samples);
}

/* ── EMA smoothing ───────────────────────────────────────────────────── */

/** Alpha = 1.0 → smoothed = current (no history). */
void test_voice_smooth_alpha_one(void)
{
    float result = voice_level_smooth(0.8f, 0.2f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, result);
}

/** Alpha = 0.0 (clamped to 0.1) → partial update. */
void test_voice_smooth_alpha_zero(void)
{
    /* alpha=0 is clamped to 0.1 */
    float result = voice_level_smooth(0.5f, 0.0f, 0.0f);
    float expected = 0.1f * 0.5f + 0.9f * 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(expected, result);
}

/** Typical alpha = 0.15 → weighted mix. */
void test_voice_smooth_typical(void)
{
    float result = voice_level_smooth(0.5f, 0.1f, 0.15f);
    float expected = 0.15f * 0.5f + 0.85f * 0.1f;
    TEST_ASSERT_EQUAL_FLOAT(expected, result);
}

/** Convergence: repeated calls with constant input approach input. */
void test_voice_smooth_convergence(void)
{
    float level = 0.0f;
    for (int i = 0; i < 100; i++) {
        level = voice_level_smooth(0.5f, level, 0.15f);
    }
    /* After 100 iterations, level should be very close to 0.5 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, level);
}

/** Smoothing of step response: rising from silence to speech. */
void test_voice_smooth_rising(void)
{
    float level = 0.0f;
    /* Simulate 20 iterations (320 ms at 16 ms per chunk) of speech */
    for (int i = 0; i < 20; i++) {
        level = voice_level_smooth(0.8f, level, 0.15f);
    }
    /* After 20 iterations, should have risen significantly */
    TEST_ASSERT_TRUE(level > 0.4f);
    TEST_ASSERT_TRUE(level < 0.8f);
}

/** Smoothing of step response: falling from speech to silence. */
void test_voice_smooth_falling(void)
{
    float level = 0.8f;
    /* Simulate 20 iterations of silence */
    for (int i = 0; i < 20; i++) {
        level = voice_level_smooth(0.0f, level, 0.15f);
    }
    /* After 20 iterations, should have fallen significantly */
    TEST_ASSERT_TRUE(level < 0.2f);
}

/* ── dBFS conversion ─────────────────────────────────────────────────── */

/** RMS = 1.0 → 0 dBFS */
void test_voice_db_full_scale(void)
{
    float db = voice_level_rms_to_db(1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, db);
}

/** RMS = 0.5 → -6 dBFS */
void test_voice_db_half_amplitude(void)
{
    float db = voice_level_rms_to_db(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.0f, db);
}

/** RMS = 0.0 → -96 dBFS (floor) */
void test_voice_db_silence(void)
{
    float db = voice_level_rms_to_db(0.0f);
    TEST_ASSERT_EQUAL_FLOAT(-96.0f, db);
}

/** RMS = 0.001 → ~ -60 dBFS */
void test_voice_db_quiet(void)
{
    float db = voice_level_rms_to_db(0.001f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -60.0f, db);
}

/** RMS = 2.0 (clamped to 1.0) → 0 dBFS */
void test_voice_db_above_full_scale(void)
{
    float db = voice_level_rms_to_db(2.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, db);
}

/** RMS = -0.5 (negative → -96 dBFS floor) */
void test_voice_db_negative(void)
{
    float db = voice_level_rms_to_db(-0.5f);
    TEST_ASSERT_EQUAL_FLOAT(-96.0f, db);
}

/* ── End-to-end: typical speech chunk → full pipeline ────────────────── */

/** Simulate one chunk of speech-like audio through the pipeline. */
void test_voice_pipeline_speech_chunk(void)
{
    /* Generate a simple speech-like signal: a 440 Hz sine at half scale */
    int16_t samples[256];
    for (int i = 0; i < 256; i++) {
        float phase = 2.0f * 3.14159265f * 440.0f * (float)i / 16000.0f;
        samples[i] = (int16_t)(14000.0f * sinf(phase));
    }

    /* Compute RMS */
    float rms = voice_level_compute_rms(samples, 256);
    TEST_ASSERT_TRUE(rms > 0.2f);
    TEST_ASSERT_TRUE(rms < 0.5f);

    /* Convert to dB */
    float db = voice_level_rms_to_db(rms);
    TEST_ASSERT_TRUE(db < -3.0f);
    TEST_ASSERT_TRUE(db > -14.0f);

    /* Smooth with previous = 0.0 (cold start) */
    float smoothed = voice_level_smooth(rms, 0.0f, 0.15f);
    TEST_ASSERT_TRUE(smoothed > 0.02f);
    TEST_ASSERT_TRUE(smoothed < rms);
}

/** Simulate transition from silence to speech. */
void test_voice_pipeline_silence_to_speech(void)
{
    float level = 0.0f;
    int16_t silence[256] = {0};
    int16_t speech[256];

    /* Generate speech samples */
    for (int i = 0; i < 256; i++) {
        float phase = 2.0f * 3.14159265f * 440.0f * (float)i / 16000.0f;
        speech[i] = (int16_t)(14000.0f * sinf(phase));
    }

    /* Start from silence */
    float silence_rms = voice_level_compute_rms(silence, 256);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, silence_rms);
    level = voice_level_smooth(silence_rms, level, 0.15f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, level);

    /* Transition to speech over several chunks */
    float speech_rms = voice_level_compute_rms(speech, 256);
    TEST_ASSERT_TRUE(speech_rms > 0.2f);

    for (int i = 0; i < 30; i++) {
        level = voice_level_smooth(speech_rms, level, 0.15f);
    }
    /* Should have risen to near speech_rms */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, speech_rms, level);
}
