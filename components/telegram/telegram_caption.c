/** @file telegram_caption.c
 * @brief Pure-function Telegram caption formatter (no ESP-IDF deps).
 *
 * Separated from telegram_client.c so the logic_tests can compile and
 * test the caption formatting without pulling in esp_http_client, cJSON,
 * and the rest of the networking stack.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include "telegram_client.h"

/**
 * Day of week for a Gregorian date via Sakamoto's algorithm.
 * @return 0=Sunday .. 6=Saturday.
 */
static int day_of_week(int year, int month, int day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

size_t telegram_format_audio_filename(const char *rec_id,
                                      char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    int written = -1;

    /* rec_id is "REC_YYYYMMDD_HHMMSS_NNN" only when SNTP has synced —
     * parse the calendar timestamp out of it for a human-readable name.
     * Offline "REC_BOOT_<uptime>_NNN" ids fail this scan and fall
     * through to the raw-id fallback below. */
    int year, month, day, hour, min, sec;
    if (rec_id &&
        sscanf(rec_id, "REC_%4d%2d%2d_%2d%2d%2d",
               &year, &month, &day, &hour, &min, &sec) == 6) {
        static const char *wd_names[7] =
            {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        int wd = day_of_week(year, month, day);
        (void)sec; /* not part of the requested HH-MM display format */
        /* Telegram silently mangles ':' in uploaded filenames (renders as a
         * space) — use '-' for the time separator instead. */
        written = snprintf(buf, buf_size, "%s, %02d.%02d.%04d, %02d-%02d.mp3",
                           wd_names[wd], day, month, year, hour, min);
    }

    if (written < 0 || (size_t)written >= buf_size) {
        written = snprintf(buf, buf_size, "%s.mp3", rec_id ? rec_id : "voice");
    }

    if (written < 0 || (size_t)written >= buf_size) {
        if (buf_size > 0) buf[buf_size - 1] = '\0';
        return 0;
    }

    return (size_t)written;
}

size_t telegram_format_caption(const char *rec_id,
                               uint32_t duration_ms,
                               const char *device_name,
                               char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    /* Convert duration_ms → MM:SS */
    uint32_t total_sec = duration_ms / 1000;
    uint32_t minutes = total_sec / 60;
    uint32_t seconds = total_sec % 60;

    int written = snprintf(buf, buf_size,
        "Recorder ID: %s\n"
        "Duration: %02" PRIu32 ":%02" PRIu32 "\n"
        "Device: %s",
        rec_id ? rec_id : "UNKNOWN",
        minutes, seconds,
        device_name ? device_name : "VoiceRecorder");

    if (written < 0 || (size_t)written >= buf_size) {
        /* Truncated — ensure NUL termination */
        if (buf_size > 0) buf[buf_size - 1] = '\0';
        return 0;
    }

    return (size_t)written;
}
