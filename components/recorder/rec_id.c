/** @file rec_id.c
 * @brief Recording ID generation — pure logic, no global state.
 */

#include "rec_id.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ── Error strings ───────────────────────────────────────────────────── */

const char *rec_id_err_str(rec_id_err_t err)
{
    switch (err) {
    case REC_ID_OK:                 return "OK";
    case REC_ID_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
    case REC_ID_ERR_COUNTER_OVERFLOW: return "Counter overflow (> 999)";
    default:                        return "unknown";
    }
}

/* ── ID generation ───────────────────────────────────────────────────── */

rec_id_err_t rec_id_generate(char *buf, size_t buf_size,
                             bool time_synced,
                             time_t now,
                             uint32_t boot_uptime_s,
                             uint32_t counter)
{
    if (!buf || buf_size == 0) {
        return REC_ID_ERR_BUFFER_TOO_SMALL;
    }

    if (counter > 999) {
        buf[0] = '\0';
        return REC_ID_ERR_COUNTER_OVERFLOW;
    }

    if (time_synced) {
        /* ── Wall-clock timestamp ────────────────────────────────── */
        struct tm tm_now;
        localtime_r(&now, &tm_now);

        int n = snprintf(buf, buf_size,
                         REC_ID_TIMESTAMP_FORMAT,
                         tm_now.tm_year + 1900,
                         tm_now.tm_mon + 1,
                         tm_now.tm_mday,
                         tm_now.tm_hour,
                         tm_now.tm_min,
                         tm_now.tm_sec,
                         (int)counter);
        if (n < 0 || (size_t)n >= buf_size) {
            buf[0] = '\0';
            return REC_ID_ERR_BUFFER_TOO_SMALL;
        }
    } else {
        /* ── Boot-relative fallback ──────────────────────────────── */
        int n = snprintf(buf, buf_size,
                         "REC_BOOT_%06" PRIu32 "_%03d",
                         boot_uptime_s, (int)counter);
        if (n < 0 || (size_t)n >= buf_size) {
            buf[0] = '\0';
            return REC_ID_ERR_BUFFER_TOO_SMALL;
        }
    }

    return REC_ID_OK;
}
