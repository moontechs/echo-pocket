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
