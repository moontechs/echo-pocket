/** @file rec_id.h
 * @brief Recording ID generation — timestamp-based or boot-relative fallback.
 *
 * Builds IDs of the form:
 *   REC_YYYYMMDD_HHMMSS_NNN   (when SNTP has synced clock — Task 14)
 *   REC_BOOT_<uptime_s>_NNN   (when offline, uses monotonic boot counter)
 *
 * Pure function (no global state) so it's fully unit-testable.
 * The caller is responsible for tracking the recording sequence counter
 * and for checking the "time synced" flag from the Wi-Fi/SNTP layer.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Maximum length of a generated recording ID string. */
#define REC_ID_MAX_LEN  48

/** Format: "REC_BOOT_<uptime_s>_<counter>" where counter ≤ 999. */
#define REC_ID_BOOT_PREFIX  "REC_BOOT_"

/** Format character that separates the timestamp from the sequence. */
#define REC_ID_TIMESTAMP_FORMAT  "REC_%04d%02d%02d_%02d%02d%02d_%03d"

/* ── Error codes ─────────────────────────────────────────────────────── */

typedef enum {
    REC_ID_OK = 0,
    REC_ID_ERR_BUFFER_TOO_SMALL,   /**< Output buffer too small             */
    REC_ID_ERR_COUNTER_OVERFLOW,   /**< Sequence counter exceeded 999       */
} rec_id_err_t;

/**
 * @brief Human-readable string for a rec_id_err_t code.
 */
const char *rec_id_err_str(rec_id_err_t err);

/* ── ID generation ───────────────────────────────────────────────────── */

/**
 * @brief Generate a recording ID string.
 *
 * If @p time_synced is true, uses the wall-clock @p now timestamp
 * (seconds since epoch) and formats it as YYYYMMDD_HHMMSS in the
 * configured timezone.  The caller must have called setenv/tzset
 * before calling this (Task 14).
 *
 * If @p time_synced is false, uses @p boot_uptime_s (monotonic seconds
 * since boot) — recordings work correctly fully offline.
 *
 * @param buf            Output buffer (at least REC_ID_MAX_LEN bytes).
 * @param buf_size       Size of @p buf.
 * @param time_synced    true if SNTP has synced the system clock.
 * @param now            Current time_t (seconds since epoch), meaningful
 *                       only when @p time_synced is true.
 * @param boot_uptime_s  Monotonic seconds since boot (used when offline).
 * @param counter        Per-boot sequence number (0-based, ≤ 999).
 *
 * @return REC_ID_OK on success, or an error code.
 */
rec_id_err_t rec_id_generate(char *buf, size_t buf_size,
                             bool time_synced,
                             time_t now,
                             uint32_t boot_uptime_s,
                             uint32_t counter);

#ifdef __cplusplus
}
#endif
