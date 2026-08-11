/** @file telegram_client.h
 * @brief Telegram Bot API client for echo-pocket.
 *
 * Sends WAV recordings as `sendDocument` via multipart/form-data,
 * streaming directly from SD without buffering the whole file in RAM.
 * Includes a `getMe` helper for token/handshake sanity checks.
 *
 * Fan-out (send_to_all / active_channel) is driven by the RecorderConfig
 * struct from Task 5.  The actual drain loop, retry cap, and
 * auto_upload/delete_after_upload logic live in upload_task (Task 17),
 * not here — this component is a single-send primitive.
 *
 * Caption format (per AGENTS.md §Telegram):
 *   Recorder ID: <rec_id>
 *   Duration: MM:SS
 *   Device: <device_name>
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/**
 * Maximum file size (bytes) accepted for upload.
 *
 * 50 MB is the Bot API sendDocument hard limit for bot-based file uploads.
 * The largest file this device produces (18–20 min mono s16 16 kHz WAV)
 * is roughly 35–38 MB, leaving ~12 MB margin.  This guard is checked
 * BEFORE streaming so a doomed upload never starts.
 */
#define TELEGRAM_MAX_UPLOAD_BYTES  52428800   /* 50 * 1024 * 1024 */

/** Maximum length of a Telegram caption string. */
#define TELEGRAM_CAPTION_MAX       256

/** Maximum hostname length for api.telegram.org */
#define TELEGRAM_HOST              "api.telegram.org"

/**
 * Telegram API base URL.  Bot token is appended at runtime by the caller:
 *   https://api.telegram.org/bot<token>/<method>
 */
#define TELEGRAM_API_PREFIX        "https://api.telegram.org/bot"

/* ── Error codes ─────────────────────────────────────────────────────── */

typedef enum {
    TELEGRAM_OK = 0,
    TELEGRAM_ERR_NULL_PARAM,         /**< Required parameter is NULL         */
    TELEGRAM_ERR_FILE_NOT_FOUND,     /**< WAV file doesn't exist on SD       */
    TELEGRAM_ERR_FILE_TOO_LARGE,     /**< File exceeds TELEGRAM_MAX_UPLOAD_BYTES */
    TELEGRAM_ERR_OOM,                /**< Memory allocation failed           */
    TELEGRAM_ERR_CONNECT,            /**< TCP/TLS connection to API failed   */
    TELEGRAM_ERR_HTTP,               /**< HTTP error (4xx, 5xx, network)    */
    TELEGRAM_ERR_API,                /**< API returned ok: false             */
    TELEGRAM_ERR_PARSE,              /**< JSON response parse failed         */
    TELEGRAM_ERR_ABORTED,            /**< Transfer aborted by caller         */
} telegram_err_t;

/** Human-readable string for each error code. */
const char *telegram_err_str(telegram_err_t err);

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the Telegram client (one-time setup).
 *
 * Must be called once before any send/getMe calls.  Not thread-safe;
 * only the upload_task should call this and the send functions.
 *
 * @param bot_token  Null-terminated bot token (e.g. "12345:AAExample").
 *                   Must remain valid for the lifetime of the client.
 * @return TELEGRAM_OK on success.
 */
telegram_err_t telegram_client_init(const char *bot_token);

/**
 * @brief De-initialise the client, freeing resources.
 */
void telegram_client_deinit(void);

/**
 * @brief Call getMe to verify connectivity and token validity.
 *
 * @param[out] out_bot_username  If non-NULL, buffer to receive the
 *                               bot's @username (must be ≥ 64 bytes).
 * @return TELEGRAM_OK on success (bot info received and ok: true).
 */
telegram_err_t telegram_client_get_me(char *out_bot_username);

/**
 * @brief Send a WAV file as a Telegram document.
 *
 * Streams from SD using esp_http_client — never buffers the full file
 * in RAM.  Checks TELEGRAM_MAX_UPLOAD_BYTES before starting.
 *
 * @param chat_id   Numeric chat_id as string (e.g. "-1001234567890")
 *                  or @username (e.g. "@mychannel").
 * @param file_path Absolute path to the WAV file on SD.
 * @param caption   Null-terminated UTF-8 caption, or NULL for none.
 * @param[out] out_message_id  If non-NULL, receives the Telegram
 *                             message_id on success.
 * @return TELEGRAM_OK on success (ok: true received).
 */
telegram_err_t telegram_client_send_document(const char *chat_id,
                                              const char *file_path,
                                              const char *caption,
                                              int *out_message_id);

/**
 * @brief Send an MP3 file as a Telegram audio message (no caption).
 *
 * Uses the Bot API `sendAudio` method rather than `sendDocument`, so the
 * file plays inline as audio in the chat. Streams from SD the same way as
 * telegram_client_send_document(). Never attaches a caption — this exists
 * specifically to send just the audio with no extra text.
 *
 * @param chat_id          Numeric chat_id as string or @username.
 * @param file_path        Absolute path to the MP3 file on SD.
 * @param upload_filename  Filename Telegram shows as the track title
 *                         (there's no caption field to carry it instead).
 *                         NULL or empty falls back to "voice.mp3".
 * @param[out] out_message_id  If non-NULL, receives the Telegram
 *                             message_id on success.
 * @return TELEGRAM_OK on success (ok: true received).
 */
telegram_err_t telegram_client_send_audio(const char *chat_id,
                                           const char *file_path,
                                           const char *upload_filename,
                                           int *out_message_id);

/**
 * @brief Convenience: send a WAV to all configured target channels.
 *
 * If cfg->send_to_all is true, sends to every channel in cfg->channels[].
 * Otherwise sends only to cfg->channels[cfg->active_channel - 1].
 *
 * Returns TELEGRAM_OK only if ALL selected targets succeeded.
 * On partial failure, returns the first error and stops.
 *
 * @param cfg        Loaded config (contains channel list + send_to_all).
 * @param file_path  Path to WAV on SD.
 * @param caption    Caption string (or NULL).
 * @param[out] out_message_id  If non-NULL, receives the last message_id.
 * @return TELEGRAM_OK on success for all targets.
 */
telegram_err_t telegram_client_send_to_channels(const RecorderConfig *cfg,
                                                 const char *file_path,
                                                 const char *caption,
                                                 int *out_message_id);

/**
 * @brief Convenience: send an MP3 (no caption) to all configured target
 * channels. Same fan-out rules as telegram_client_send_to_channels().
 *
 * @param cfg              Loaded config (contains channel list + send_to_all).
 * @param file_path        Path to MP3 on SD.
 * @param upload_filename  Filename Telegram shows as the track title
 *                         (NULL/empty falls back to "voice.mp3").
 * @param[out] out_message_id  If non-NULL, receives the last message_id.
 * @return TELEGRAM_OK on success for all targets.
 */
telegram_err_t telegram_client_send_audio_to_channels(const RecorderConfig *cfg,
                                                       const char *file_path,
                                                       const char *upload_filename,
                                                       int *out_message_id);

/* ── Pure-logic helpers (unit-testable) ──────────────────────────────── */

/**
 * @brief Format a Telegram upload caption.
 *
 * Pure function — takes rec id, duration in ms, and device name,
 * produces the exact caption string from AGENTS.md §Telegram:
 *
 *   Recorder ID: REC_20260804_215700_001
 *   Duration: 03:04
 *   Device: VoiceRecorder
 *
 * @param rec_id       Recording ID string (e.g. "REC_20260804_215700_001").
 * @param duration_ms  Duration in milliseconds.
 * @param device_name  Device name from config (e.g. "VoiceRecorder").
 * @param buf          Output buffer.
 * @param buf_size     Size of output buffer.
 * @return  Number of bytes written (excluding NUL), or 0 if buf is too small.
 */
size_t telegram_format_caption(const char *rec_id,
                               uint32_t duration_ms,
                               const char *device_name,
                               char *buf, size_t buf_size);

/**
 * @brief Format the uploaded-audio filename shown as the track title in
 * Telegram's inline player (since sendAudio carries no caption).
 *
 * Pure function. When @p rec_id is a clock-synced
 * "REC_YYYYMMDD_HHMMSS_NNN" id, produces "Ddd, DD.MM.YYYY, HH-MM.mp3"
 * (device local time, weekday computed from the calendar date — no
 * timezone/DST dependency; hyphen rather than colon because Telegram
 * mangles ':' in uploaded filenames, rendering it as a space).
 *
 * When @p rec_id is instead an offline "REC_BOOT_<uptime_s>_NNN" id — a
 * recording made before SNTP had synced — and the caller passes a nonzero
 * @p now (current wall clock, post-sync) and @p now_uptime_s (current
 * monotonic uptime), the original recording's wall-clock time is
 * reconstructed as `now - (now_uptime_s - uptime_s)` and formatted the same
 * way. This fixes filenames for recordings that get queued and uploaded
 * later (e.g. no Wi-Fi at record time), which would otherwise upload with
 * the meaningless boot-relative id baked into the name. Pass 0 for both to
 * skip reconstruction (falls back to "<rec_id>.mp3"), e.g. when time is
 * still unsynced.
 *
 * @param rec_id       Recording ID string.
 * @param now          Current wall-clock time (time(NULL)), or 0 if unknown.
 * @param now_uptime_s Current monotonic seconds since boot, or 0 if unknown.
 * @param buf          Output buffer.
 * @param buf_size     Size of output buffer.
 * @return  Number of bytes written (excluding NUL), or 0 if buf is too small.
 */
size_t telegram_format_audio_filename(const char *rec_id,
                                      time_t now, uint32_t now_uptime_s,
                                      char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
