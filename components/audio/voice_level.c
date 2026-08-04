/** @file voice_level.c
 * @brief Pure-logic voice level computation for unit testing.
 */

#include "voice_level.h"
#include <math.h>
#include <stdint.h>

/* ── RMS computation ─────────────────────────────────────────────────── */

float voice_level_compute_rms(const int16_t *samples, size_t count)
{
    if (!samples || count == 0) {
        return 0.0f;
    }

    /* Sum of squares in int64_t to avoid overflow for large chunks.
     * Max per sample: 32767² = 1,073,676,289.
     * For a 256-sample chunk: max sum ≈ 275 billion → fits in int64_t. */
    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum_sq += (int64_t)s * s;
    }

    /* Mean of squares */
    double mean_sq = (double)sum_sq / (double)count;

    /* RMS / full-scale */
    double rms = sqrt(mean_sq) / 32767.0;
    if (rms > 1.0) rms = 1.0;

    return (float)rms;
}

/* ── EMA smoothing ───────────────────────────────────────────────────── */

float voice_level_smooth(float current, float previous, float alpha)
{
    /* Clamp alpha to (0.0, 1.0] for safety */
    if (alpha <= 0.0f) alpha = 0.1f;
    if (alpha > 1.0f)  alpha = 1.0f;

    return alpha * current + (1.0f - alpha) * previous;
}

/* ── dBFS conversion ─────────────────────────────────────────────────── */

float voice_level_rms_to_db(float level)
{
    if (level <= 0.0f) {
        return -96.0f;
    }
    if (level >= 1.0f) {
        return 0.0f;
    }

    float db = 20.0f * log10f(level);
    if (db < -96.0f) db = -96.0f;
    return db;
}
