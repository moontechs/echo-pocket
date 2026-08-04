/** @file recorder_split.h
 * @brief Auto-split threshold check — pure function, fully testable.
 *
 * This is a minimal header that does NOT pull in FreeRTOS or audio
 * dependencies, so it can be included by logic_tests and by
 * recorder_logic.c without dragging in the full recorder stack.
 *
 * recorder.h re-exports everything declared here.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Auto-split threshold in bytes.
 *
 * 19 minutes at 16000 Hz mono s16:
 *   19 × 60 × 16000 × 2 = 36,480,000 bytes ≈ 34.8 MB.
 * Well under the 50 MB Telegram Bot API limit.
 */
#define RECORDER_SPLIT_BYTES  ((uint32_t)(19 * 60 * 16000 * sizeof(int16_t)))

/**
 * @brief Check whether the given byte count exceeds the auto-split
 *        threshold.  Pure function — unit-testable without hardware.
 *
 * @param bytes_written  Current WAV data size in bytes.
 * @return true if a split should occur at this point.
 */
bool recorder_should_split(uint32_t bytes_written);

#ifdef __cplusplus
}
#endif
