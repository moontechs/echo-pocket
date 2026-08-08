/** @file config.h
 * @brief INI config parser for recorder.ini + atomic write-back.
 *
 * Reads /sdcard/echo-pocket/config/recorder.ini into a RecorderConfig
 * struct.  Missing or malformed files fall back to safe defaults — the
 * firmware never crashes or aborts boot because of a config problem.
 *
 * ── CONFIG KEY AUDIT (which key is consumed where in this plan) ─────────
 *
 * [device]
 *   name       → Task 16 (upload caption "Device:" line)
 *   timezone   → Task 14 (setenv("TZ",…) / tzset() after SNTP sync)
 *
 * [wifi_N]     → Task 14 (network connection, tried in N order)
 *   ssid
 *   password
 *
 * [telegram]
 *   bot_token      → Task 16 (Bot API auth header)
 *   send_to_all    → Task 16/17 (fan-out target selection)
 *   active_channel → Task 16/17 (which channel_N_* to use for single-target)
 *   channel_N_id   → Task 16 (numeric chat_id or @username)
 *   channel_N_name → Task 16 (display-only in UI, not sent on wire)
 *
 * [recorder]
 *   auto_upload       → Task 17 (gates automatic drain; manual "Send All" still works)
 *   delete_after_upload→ Task 17 (deletes WAV only after queue entry is durably "sent")
 *   sample_rate       → Task 6  (parsed but fixed at 16000 for v1.0 — key is INERT)
 *   noise_suppression → Task 8  (ESP-SR AFE NS enable/disable gate)
 *   voice_detection   → Task 8  (ESP-SR AFE VAD enable/disable gate)
 *
 * [face]
 *   theme          → Task 9  (registry resolve; unknown → "vector")
 *   react_to_voice → Task 10 (eye-size reactivity gate)
 *   eye_min_size   → Task 10 (eye interpolation lower bound)
 *   eye_max_size   → Task 10 (eye interpolation upper bound)
 *   blink          → Task 10 (blink animation gate)
 *   animation_fps  → Task 9  (frame-rate cap enforced in registry/UI task)
 *
 * Every key listed in AGENTS.md §Config is either wired above (consumed)
 * or noted as INERT for v1.0 (sample_rate).  No key is silently dropped.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Maximum sizes (tuned to the AGENTS.md §Config example) ──────────── */

#define CONFIG_MAX_DEVICE_NAME   64
#define CONFIG_MAX_TIMEZONE      64
#define CONFIG_MAX_SSID          33   /* 802.11 max */
#define CONFIG_MAX_PASSWORD      64   /* WPA2 passphrase max 63 + NUL */
#define CONFIG_MAX_BOT_TOKEN     128
#define CONFIG_MAX_CHANNEL_ID    64   /* numeric chat_id or @username */
#define CONFIG_MAX_CHANNEL_NAME  64
#define CONFIG_MAX_THEME_NAME    32
#define CONFIG_MAX_WIFI_NETWORKS 5
#define CONFIG_MAX_CHANNELS      5

/* ── Error codes ─────────────────────────────────────────────────────── */

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_FILE_NOT_FOUND,     /**< INI file doesn't exist — defaults used */
    CONFIG_ERR_READ_FAILED,        /**< fopen/fread error — defaults used   */
    CONFIG_ERR_WRITE_FAILED,       /**< Could not write temp or final file  */
    CONFIG_ERR_RENAME_FAILED,      /**< Atomic rename .tmp → .ini failed    */
    CONFIG_ERR_BUFFER_TOO_SMALL,   /**< Serialization buffer too small      */
} config_err_t;

const char *config_err_str(config_err_t err);

/* ── Sub-structs ─────────────────────────────────────────────────────── */

typedef struct {
    char ssid[CONFIG_MAX_SSID];
    char password[CONFIG_MAX_PASSWORD];
} wifi_network_t;

typedef struct {
    char id[CONFIG_MAX_CHANNEL_ID];        /* numeric chat_id or @username */
    char name[CONFIG_MAX_CHANNEL_NAME];    /* display-only label           */
} telegram_channel_t;

/* ── Main config struct ──────────────────────────────────────────────── */

typedef struct {
    /* [device] */
    char device_name[CONFIG_MAX_DEVICE_NAME];
    char timezone[CONFIG_MAX_TIMEZONE];

    /* [wifi_N] — parsed from sections named wifi_1, wifi_2, … */
    wifi_network_t wifi_networks[CONFIG_MAX_WIFI_NETWORKS];
    int wifi_count;

    /* [telegram] */
    char bot_token[CONFIG_MAX_BOT_TOKEN];
    bool send_to_all;
    int  active_channel;                    /* 1-based index               */
    telegram_channel_t channels[CONFIG_MAX_CHANNELS];
    int channel_count;

    /* [recorder] */
    bool auto_upload;
    bool delete_after_upload;
    int  sample_rate;                       /* INERT in v1.0 (fixed 16000) */
    bool noise_suppression;
    bool voice_detection;

    /* [face] */
    char theme[CONFIG_MAX_THEME_NAME];
    bool react_to_voice;
    int  eye_min_size;
    int  eye_max_size;
    bool blink;
    int  animation_fps;
} RecorderConfig;

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * Fill @p cfg with safe defaults (as if no config file existed).
 * Call this before `config_parse()` so any key NOT present in the file
 * keeps its default value.
 */
void config_set_defaults(RecorderConfig *cfg);

/**
 * Parse INI content from a null-terminated string into @p cfg.
 *
 * @p cfg should be initialised with `config_set_defaults()` first —
 * keys that are missing from the INI keep their defaults.
 *
 * @return  Number of parse errors encountered (0 = clean parse).
 *          Parse errors are skipped — the struct is always usable.
 */
int config_parse(RecorderConfig *cfg, const char *data);

/**
 * Load config from the INI file at @p path.
 *
 * On missing/unreadable file: fills @p cfg with defaults, returns
 * CONFIG_ERR_FILE_NOT_FOUND or CONFIG_ERR_READ_FAILED.  Never crashes.
 *
 * @param[out] out_err  If non-NULL, receives the error code.
 * @return  Populated config struct (always valid, may be all-defaults).
 */
RecorderConfig config_load(const char *path, config_err_t *out_err);

/**
 * Serialize @p cfg to an INI string in @p buf.
 *
 * @return  Number of bytes written (excluding null terminator),
 *          or 0 if @p buf_len is too small.  The output is always
 *          null-terminated if @p buf_len > 0.
 */
size_t config_serialize(const RecorderConfig *cfg, char *buf, size_t buf_len);

/**
 * Save @p cfg atomically to @p path.
 *
 * Writes to `<path>.tmp` then renames to @p path — a power loss
 * mid-write leaves the original file intact.
 *
 * @return CONFIG_OK on success, or an error code.
 */
config_err_t config_save(const RecorderConfig *cfg, const char *path);

#ifdef __cplusplus
}
#endif
