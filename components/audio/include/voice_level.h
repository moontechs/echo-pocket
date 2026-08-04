/** @file voice_level.h
 * @brief Voice level computation — RMS, smoothing, dB conversion.
 *
 * Pure functions with no ESP-IDF or hardware dependencies so they
 * can be unit-tested under logic_tests.  The AFE itself is not
 * tested here — only the glue math around it.
 *
 * Range convention: all level values are in [0.0, 1.0] where
 * 1.0 = full-scale (INT16_MAX).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Compute the RMS (root-mean-square) level of a mono PCM chunk.
 *
 * RMS = sqrt(mean(sample²)) / INT16_MAX.
 *
 * @param samples  Mono int16_t samples.
 * @param count    Number of samples (may be 0 → returns 0.0).
 * @return  Level in [0.0, 1.0].
 */
float voice_level_compute_rms(const int16_t *samples, size_t count);

/**
 * @brief Apply exponential moving average (EMA) smoothing.
 *
 * smoothed = alpha * current + (1 - alpha) * previous
 *
 * @param current   Newly computed level.
 * @param previous  Previously smoothed level.
 * @param alpha     Smoothing factor in (0.0, 1.0].  Smaller = more smoothing.
 * @return  Smoothed level.
 */
float voice_level_smooth(float current, float previous, float alpha);

/**
 * @brief Convert a linear RMS level to decibels (dBFS).
 *
 * dBFS = 20 * log10(level), clamped to [-96, 0].
 *
 * @param level  Linear level in [0.0, 1.0].
 * @return  Level in dBFS (≤ 0).
 */
float voice_level_rms_to_db(float level);

#ifdef __cplusplus
}
#endif
