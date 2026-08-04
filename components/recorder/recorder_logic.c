/** @file recorder_logic.c
 * @brief Pure logic functions from the recorder component.
 *
 * These are separated from recorder.c so they can be unit-tested
 * under logic_tests without pulling in FreeRTOS, esp_timer, or the
 * full recorder task infrastructure.
 *
 * All functions in this file must be pure (no global state, no I/O,
 * no FreeRTOS calls).
 */

#include "recorder_split.h"

/* ── Auto-split threshold ────────────────────────────────────────────── */

bool recorder_should_split(uint32_t bytes_written)
{
    return bytes_written >= RECORDER_SPLIT_BYTES;
}
