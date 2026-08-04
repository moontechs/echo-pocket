/** @file config.c
 * @brief INI parser + atomic write-back for recorder.ini.
 *
 * Pure functions (config_set_defaults, config_parse, config_serialize)
 * are testable without a filesystem; config_load / config_save wrap them
 * with file I/O.
 */

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sd_storage.h"

static const char *TAG = "config";

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Maximum line length for INI parsing. Longer lines are truncated. */
#define CONFIG_LINE_MAX  256

/** Section currently being parsed (index into section name table). */
typedef enum {
    SEC_NONE = 0,
    SEC_DEVICE,
    SEC_WIFI,       /* wifi_N — dynamic index stored separately */
    SEC_TELEGRAM,
    SEC_RECORDER,
    SEC_FACE,
    SEC_UNKNOWN,    /* section we don't recognise — skip its keys   */
} section_kind_t;

/** Null-safe strncpy that guarantees NUL termination. */
static void safe_copy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/** Strip leading and trailing whitespace from a mutable buffer.
 *  Returns the new start pointer; the string is NUL-terminated in place. */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/** Case-insensitive string comparison. */
static bool streq_ci(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/** Try to parse an integer from s, returning def on failure. */
static int parse_int(const char *s, int def)
{
    if (!s || !*s) return def;
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || *end != '\0') return def;
    return (int)val;
}

/** Parse "true"/"false" / "1"/"0" / "yes"/"no", case-insensitive. */
static bool parse_bool(const char *s, bool def)
{
    if (!s || !*s) return def;
    if (streq_ci(s, "true") || streq_ci(s, "1") || streq_ci(s, "yes"))
        return true;
    if (streq_ci(s, "false") || streq_ci(s, "0") || streq_ci(s, "no"))
        return false;
    return def;
}

/* ── Defaults ────────────────────────────────────────────────────────── */

void config_set_defaults(RecorderConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    /* [device] */
    safe_copy(cfg->device_name, "VoiceRecorder", CONFIG_MAX_DEVICE_NAME);
    safe_copy(cfg->timezone, "UTC", CONFIG_MAX_TIMEZONE);

    /* [wifi_N] — empty by default */
    cfg->wifi_count = 0;

    /* [telegram] */
    cfg->bot_token[0] = '\0';              /* disabled until set */
    cfg->send_to_all  = false;
    cfg->active_channel = 1;               /* first channel        */
    cfg->channel_count  = 0;

    /* [recorder] */
    cfg->auto_upload        = true;
    cfg->delete_after_upload = false;
    cfg->sample_rate         = 16000;      /* INERT in v1.0        */
    cfg->noise_suppression   = true;
    cfg->voice_detection     = true;

    /* [face] */
    safe_copy(cfg->theme, "minimal", CONFIG_MAX_THEME_NAME);
    cfg->react_to_voice = true;
    cfg->eye_min_size   = 5;
    cfg->eye_max_size   = 22;
    cfg->blink          = true;
    cfg->animation_fps  = 20;
}

/* ── Error strings ───────────────────────────────────────────────────── */

const char *config_err_str(config_err_t err)
{
    switch (err) {
    case CONFIG_OK:                return "OK";
    case CONFIG_ERR_FILE_NOT_FOUND: return "Config file not found";
    case CONFIG_ERR_READ_FAILED:   return "Config file read failed";
    case CONFIG_ERR_WRITE_FAILED:  return "Config write failed";
    case CONFIG_ERR_RENAME_FAILED: return "Config atomic rename failed";
    case CONFIG_ERR_BUFFER_TOO_SMALL: return "Serialization buffer too small";
    default:                       return "unknown";
    }
}

/* ── INI parser ──────────────────────────────────────────────────────── */

/**
 * Apply a key=value pair to the config, given the current section context.
 *
 * @param cfg       Config being built.
 * @param kind      Section kind (SEC_DEVICE / SEC_TELEGRAM / …).
 * @param wifi_idx  Valid only when kind == SEC_WIFI; the 0-based index.
 * @param key       Already-trimmed key.
 * @param value     Already-trimmed value.
 */
static void apply_key(RecorderConfig *cfg, section_kind_t kind,
                      int wifi_idx, const char *key, const char *value)
{
    if (!cfg || !key || !value) return;
    if (kind == SEC_NONE || kind == SEC_UNKNOWN) return;
    if (*key == '\0') return;

    /* ── [device] ────────────────────────────────────────────────── */
    if (kind == SEC_DEVICE) {
        if (streq_ci(key, "name")) {
            safe_copy(cfg->device_name, value, CONFIG_MAX_DEVICE_NAME);
        } else if (streq_ci(key, "timezone")) {
            safe_copy(cfg->timezone, value, CONFIG_MAX_TIMEZONE);
        }
        return;
    }

    /* ── [wifi_N] ────────────────────────────────────────────────── */
    if (kind == SEC_WIFI && wifi_idx >= 0 &&
        wifi_idx < CONFIG_MAX_WIFI_NETWORKS) {
        if (streq_ci(key, "ssid")) {
            safe_copy(cfg->wifi_networks[wifi_idx].ssid, value,
                      CONFIG_MAX_SSID);
        } else if (streq_ci(key, "password")) {
            safe_copy(cfg->wifi_networks[wifi_idx].password, value,
                      CONFIG_MAX_PASSWORD);
        }
        return;
    }

    /* ── [telegram] ──────────────────────────────────────────────── */
    if (kind == SEC_TELEGRAM) {
        if (streq_ci(key, "bot_token")) {
            safe_copy(cfg->bot_token, value, CONFIG_MAX_BOT_TOKEN);
        } else if (streq_ci(key, "send_to_all")) {
            cfg->send_to_all = parse_bool(value, false);
        } else if (streq_ci(key, "active_channel")) {
            cfg->active_channel = parse_int(value, 1);
        }
        /* channel_N_id / channel_N_name */
        else if (strncmp(key, "channel_", 8) == 0) {
            const char *p = key + 8; /* skip "channel_" */
            int idx = parse_int(p, -1);
            if (idx >= 1 && idx <= CONFIG_MAX_CHANNELS) {
                int ci = idx - 1;
                if (ci >= cfg->channel_count) {
                    cfg->channel_count = ci + 1;
                }
                const char *rest = p;
                while (*rest && *rest != '_') rest++;
                if (*rest == '_') rest++;
                if (streq_ci(rest, "id")) {
                    safe_copy(cfg->channels[ci].id, value,
                              CONFIG_MAX_CHANNEL_ID);
                } else if (streq_ci(rest, "name")) {
                    safe_copy(cfg->channels[ci].name, value,
                              CONFIG_MAX_CHANNEL_NAME);
                }
            }
        }
        return;
    }

    /* ── [recorder] ──────────────────────────────────────────────── */
    if (kind == SEC_RECORDER) {
        if (streq_ci(key, "auto_upload")) {
            cfg->auto_upload = parse_bool(value, true);
        } else if (streq_ci(key, "delete_after_upload")) {
            cfg->delete_after_upload = parse_bool(value, false);
        } else if (streq_ci(key, "sample_rate")) {
            cfg->sample_rate = parse_int(value, 16000);
        } else if (streq_ci(key, "noise_suppression")) {
            cfg->noise_suppression = parse_bool(value, true);
        } else if (streq_ci(key, "voice_detection")) {
            cfg->voice_detection = parse_bool(value, true);
        }
        return;
    }

    /* ── [face] ──────────────────────────────────────────────────── */
    if (kind == SEC_FACE) {
        if (streq_ci(key, "theme")) {
            safe_copy(cfg->theme, value, CONFIG_MAX_THEME_NAME);
        } else if (streq_ci(key, "react_to_voice")) {
            cfg->react_to_voice = parse_bool(value, true);
        } else if (streq_ci(key, "eye_min_size")) {
            cfg->eye_min_size = parse_int(value, 5);
        } else if (streq_ci(key, "eye_max_size")) {
            cfg->eye_max_size = parse_int(value, 22);
        } else if (streq_ci(key, "blink")) {
            cfg->blink = parse_bool(value, true);
        } else if (streq_ci(key, "animation_fps")) {
            cfg->animation_fps = parse_int(value, 20);
        }
        return;
    }
}

/** Parse a line. Returns a static string with the parse outcome. */
static const char *parse_line(RecorderConfig *cfg,
                              const char *line,
                              section_kind_t *cur_kind,
                              int *wifi_idx)
{
    char buf[CONFIG_LINE_MAX];
    safe_copy(buf, line, sizeof(buf));
    char *s = trim(buf);

    /* Empty line or comment */
    if (*s == '\0' || *s == '#' || *s == ';') {
        return "comment/empty";
    }

    /* Section header: [something] */
    if (*s == '[') {
        char *close = strchr(s, ']');
        if (!close) {
            return "malformed section — no ']'";
        }
        *close = '\0';
        char *sec_name = trim(s + 1);

        if (streq_ci(sec_name, "device")) {
            *cur_kind = SEC_DEVICE;
            return "section device";
        } else if (streq_ci(sec_name, "telegram")) {
            *cur_kind = SEC_TELEGRAM;
            return "section telegram";
        } else if (streq_ci(sec_name, "recorder")) {
            *cur_kind = SEC_RECORDER;
            return "section recorder";
        } else if (streq_ci(sec_name, "face")) {
            *cur_kind = SEC_FACE;
            return "section face";
        } else if (strncmp(sec_name, "wifi_", 5) == 0) {
            *cur_kind = SEC_WIFI;
            *wifi_idx = parse_int(sec_name + 5, -1) - 1; /* 0-based */
            if (*wifi_idx < 0 || *wifi_idx >= CONFIG_MAX_WIFI_NETWORKS) {
                *cur_kind = SEC_UNKNOWN;
                return "wifi_N index out of range";
            }
            /* Grow wifi_count if this is the highest index seen */
            if (*wifi_idx + 1 > cfg->wifi_count) {
                cfg->wifi_count = *wifi_idx + 1;
            }
            return "section wifi_N";
        } else {
            *cur_kind = SEC_UNKNOWN;
            return "section unknown";
        }
    }

    /* Key = value */
    char *eq = strchr(s, '=');
    if (!eq) {
        return "no '=' in line";
    }
    *eq = '\0';
    char *key = trim(s);
    char *val = trim(eq + 1);

    apply_key(cfg, *cur_kind, *wifi_idx, key, val);
    return "key=value";
}

/* ── Public parse entry point ────────────────────────────────────────── */

int config_parse(RecorderConfig *cfg, const char *data)
{
    if (!cfg) return 0;

    section_kind_t cur_kind = SEC_NONE;
    int wifi_idx = -1;
    int errors = 0;

    if (!data || *data == '\0') {
        /* Empty input — just use defaults as-is. */
        return 0;
    }

    /* Walk line by line.  We take a mutable copy so trim() can work. */
    char *copy = strdup(data);
    if (!copy) return 0;

    char *saveptr = NULL;
    char *line = strtok_r(copy, "\r\n", &saveptr);
    while (line) {
        const char *result = parse_line(cfg, line, &cur_kind, &wifi_idx);
        if (result &&
            strncmp(result, "malformed", 9) == 0) {
            errors++;
            ESP_LOGW(TAG, "Parse error: %s (line: '%s')", result, line);
        } else if (result &&
                   strncmp(result, "no '='", 6) == 0) {
            errors++;
            ESP_LOGW(TAG, "Parse error: %s (line: '%s')", result, line);
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    free(copy);
    return errors;
}

/* ── File load ───────────────────────────────────────────────────────── */

RecorderConfig config_load(const char *path, config_err_t *out_err)
{
    RecorderConfig cfg;
    config_set_defaults(&cfg);

    if (!path) {
        if (out_err) *out_err = CONFIG_ERR_FILE_NOT_FOUND;
        return cfg;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (out_err) *out_err = CONFIG_ERR_FILE_NOT_FOUND;
        ESP_LOGW(TAG, "Config file not found: %s — using defaults", path);
        return cfg;
    }

    /* Read entire file into buffer */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        if (out_err) *out_err = CONFIG_OK;  /* empty file is valid */
        return cfg;
    }
    rewind(f);

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        if (out_err) *out_err = CONFIG_ERR_READ_FAILED;
        ESP_LOGE(TAG, "Out of memory reading config");
        return cfg;
    }

    size_t read = fread(buf, 1, size, f);
    fclose(f);
    buf[read] = '\0';

    int errors = config_parse(&cfg, buf);
    free(buf);

    if (errors > 0) {
        ESP_LOGW(TAG, "Config parsed with %d error(s) — some keys may have defaults", errors);
    } else {
        ESP_LOGI(TAG, "Config loaded successfully from %s", path);
    }

    if (out_err) *out_err = CONFIG_OK;
    return cfg;
}

/* ── Serialize ───────────────────────────────────────────────────────── */

/**
 * Helper: write a key=value line to buf, advancing pos.
 * Returns true if there was enough space.
 */
static bool append_kv(char *buf, size_t buf_len, size_t *pos,
                      const char *key, const char *value)
{
    int needed = snprintf(buf + *pos, buf_len - *pos, "%s=%s\n", key, value);
    if (needed < 0 || (size_t)needed >= buf_len - *pos) return false;
    *pos += needed;
    return true;
}

static bool append_kv_int(char *buf, size_t buf_len, size_t *pos,
                          const char *key, int value)
{
    int needed = snprintf(buf + *pos, buf_len - *pos, "%s=%d\n", key, value);
    if (needed < 0 || (size_t)needed >= buf_len - *pos) return false;
    *pos += needed;
    return true;
}

static bool append_kv_bool(char *buf, size_t buf_len, size_t *pos,
                           const char *key, bool value)
{
    return append_kv(buf, buf_len, pos, key, value ? "true" : "false");
}

static bool append_section(char *buf, size_t buf_len, size_t *pos,
                           const char *section)
{
    int needed = snprintf(buf + *pos, buf_len - *pos, "\n[%s]\n", section);
    if (needed < 0 || (size_t)needed >= buf_len - *pos) return false;
    *pos += needed;
    return true;
}

size_t config_serialize(const RecorderConfig *cfg, char *buf, size_t buf_len)
{
    if (!cfg || !buf || buf_len == 0) return 0;
    buf[0] = '\0';

    size_t pos = 0;
    bool ok = true;

    /* [device] */
    ok = ok && append_section(buf, buf_len, &pos, "device");
    ok = ok && append_kv(buf, buf_len, &pos, "name", cfg->device_name);
    ok = ok && append_kv(buf, buf_len, &pos, "timezone", cfg->timezone);

    /* [wifi_N] */
    for (int i = 0; i < cfg->wifi_count; i++) {
        char sec_name[16];
        snprintf(sec_name, sizeof(sec_name), "wifi_%d", i + 1);
        ok = ok && append_section(buf, buf_len, &pos, sec_name);
        ok = ok && append_kv(buf, buf_len, &pos, "ssid",
                             cfg->wifi_networks[i].ssid);
        ok = ok && append_kv(buf, buf_len, &pos, "password",
                             cfg->wifi_networks[i].password);
    }

    /* [telegram] */
    ok = ok && append_section(buf, buf_len, &pos, "telegram");
    ok = ok && append_kv(buf, buf_len, &pos, "bot_token", cfg->bot_token);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "send_to_all", cfg->send_to_all);
    ok = ok && append_kv_int(buf, buf_len, &pos, "active_channel", cfg->active_channel);
    for (int i = 0; i < cfg->channel_count; i++) {
        char key_id[32], key_name[32];
        snprintf(key_id,   sizeof(key_id),   "channel_%d_id", i + 1);
        snprintf(key_name, sizeof(key_name), "channel_%d_name", i + 1);
        ok = ok && append_kv(buf, buf_len, &pos, key_id, cfg->channels[i].id);
        ok = ok && append_kv(buf, buf_len, &pos, key_name, cfg->channels[i].name);
    }

    /* [recorder] */
    ok = ok && append_section(buf, buf_len, &pos, "recorder");
    ok = ok && append_kv_bool(buf, buf_len, &pos, "auto_upload", cfg->auto_upload);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "delete_after_upload", cfg->delete_after_upload);
    ok = ok && append_kv_int(buf, buf_len, &pos, "sample_rate", cfg->sample_rate);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "noise_suppression", cfg->noise_suppression);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "voice_detection", cfg->voice_detection);

    /* [face] */
    ok = ok && append_section(buf, buf_len, &pos, "face");
    ok = ok && append_kv(buf, buf_len, &pos, "theme", cfg->theme);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "react_to_voice", cfg->react_to_voice);
    ok = ok && append_kv_int(buf, buf_len, &pos, "eye_min_size", cfg->eye_min_size);
    ok = ok && append_kv_int(buf, buf_len, &pos, "eye_max_size", cfg->eye_max_size);
    ok = ok && append_kv_bool(buf, buf_len, &pos, "blink", cfg->blink);
    ok = ok && append_kv_int(buf, buf_len, &pos, "animation_fps", cfg->animation_fps);

    if (!ok) {
        buf[0] = '\0';
        return 0;
    }

    return pos;
}

/* ── Atomic save ─────────────────────────────────────────────────────── */

config_err_t config_save(const RecorderConfig *cfg, const char *path)
{
    if (!cfg || !path) return CONFIG_ERR_WRITE_FAILED;

    /* Serialize into a generous stack buffer — config is small enough
     * that dynamic allocation isn't worth the complexity. */
    char buf[2048];
    size_t len = config_serialize(cfg, buf, sizeof(buf));
    if (len == 0) return CONFIG_ERR_BUFFER_TOO_SMALL;

    /* Build temp path: <path>.tmp */
    char tmp_path[256];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        return CONFIG_ERR_BUFFER_TOO_SMALL;
    }

    /* Write to temp file */
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for writing", tmp_path);
        return CONFIG_ERR_WRITE_FAILED;
    }

    size_t written = fwrite(buf, 1, len, f);
    int close_ret = fclose(f);

    if (written != len || close_ret != 0) {
        ESP_LOGE(TAG, "Write to %s failed", tmp_path);
        remove(tmp_path); /* best-effort cleanup */
        return CONFIG_ERR_WRITE_FAILED;
    }

    /* Atomic rename */
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Rename %s → %s failed", tmp_path, path);
        remove(tmp_path); /* best-effort cleanup */
        return CONFIG_ERR_RENAME_FAILED;
    }

    ESP_LOGI(TAG, "Config saved to %s (%zu bytes)", path, len);
    return CONFIG_OK;
}
