/** @file queue_store.c
 * @brief Persistent upload queue — JSON index, atomic writes, crash recovery.
 *
 * Stores /echo-pocket/queue/index.json with the schema from AGENTS.md
 * §Upload queue.  On init, any "uploading" entry → "pending" (crash
 * recovery).  All disk writes use temp-file + rename (atomic).
 *
 * Pure-logic functions (queue_serialize, queue_deserialize,
 * queue_recover_uploading) are at the bottom and have no filesystem
 * or ESP-IDF dependencies — they are tested under logic_tests.
 */

#include "queue_store.h"
#include "sd_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "queue_store";

/* ── Opaque handle ───────────────────────────────────────────────────── */

struct queue_index_s {
    queue_entry_t *entries;           /**< Dynamic array of entries (heap)    */
    int            count;             /**< Current entry count                */
    int            capacity;          /**< Allocated capacity                 */
    char           index_path[320];   /**< Full path to index.json file       */
};

/* ── State string table ──────────────────────────────────────────────── */

static const char *kStateStrings[] = {
    [QUEUE_STATE_RECORDING] = "recording",
    [QUEUE_STATE_PENDING]   = "pending",
    [QUEUE_STATE_UPLOADING] = "uploading",
    [QUEUE_STATE_SENT]      = "sent",
    [QUEUE_STATE_FAILED]    = "failed",
};

const char *queue_state_str(queue_state_t state)
{
    if (state > QUEUE_STATE_FAILED) return "unknown";
    return kStateStrings[state];
}

queue_state_t queue_state_from_str(const char *s)
{
    if (!s) return QUEUE_STATE_PENDING;
    for (int i = 0; i <= QUEUE_STATE_FAILED; i++) {
        if (strcmp(s, kStateStrings[i]) == 0) {
            return (queue_state_t)i;
        }
    }
    return QUEUE_STATE_PENDING;  /* fallback */
}

/* ── Error strings ───────────────────────────────────────────────────── */

const char *queue_store_err_str(queue_store_err_t err)
{
    switch (err) {
    case QUEUE_STORE_OK:                return "OK";
    case QUEUE_STORE_ERR_NOT_INITIALIZED: return "Not initialized";
    case QUEUE_STORE_ERR_FULL:          return "Queue full";
    case QUEUE_STORE_ERR_IO:            return "I/O error";
    case QUEUE_STORE_ERR_PARSE:         return "JSON parse error";
    case QUEUE_STORE_ERR_NOT_FOUND:     return "Entry not found";
    default:                            return "unknown";
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Null-safe strncpy with guaranteed NUL termination. */
static void safe_copy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/* ── Atomic file write (wraps pure queue_serialize) ──────────────────── */

/**
 * Persist the queue to disk atomically (write temp file + rename).
 */
static queue_store_err_t queue_store_flush(queue_index_t *queue)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (queue->index_path[0] == '\0') return QUEUE_STORE_ERR_NOT_INITIALIZED;

    /* Serialize to heap buffer (queue can be larger than stack-safe) */
    size_t buf_size = 4096 + (queue->count * 256);  /* generous estimate */
    char *buf = malloc(buf_size);
    if (!buf) return QUEUE_STORE_ERR_IO;

    size_t len = queue_serialize(queue->entries, queue->count, buf, buf_size);
    if (len == 0) {
        free(buf);
        ESP_LOGE(TAG, "Serialization failed for %d entries", queue->count);
        return QUEUE_STORE_ERR_IO;
    }

    /* Build temp path */
    char tmp_path[340];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", queue->index_path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        free(buf);
        return QUEUE_STORE_ERR_IO;
    }

    /* Write temp file */
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for write", tmp_path);
        free(buf);
        return QUEUE_STORE_ERR_IO;
    }

    size_t written = fwrite(buf, 1, len, f);
    int close_ret = fclose(f);
    free(buf);

    if (written != len || close_ret != 0) {
        ESP_LOGE(TAG, "Write to %s failed", tmp_path);
        remove(tmp_path);
        return QUEUE_STORE_ERR_IO;
    }

    /* Atomic rename. FatFs's rename() fails with FR_EXIST if the
     * destination already exists (true for every flush after the
     * first), so remove it first — same fix as config_save(). */
    remove(queue->index_path);
    if (rename(tmp_path, queue->index_path) != 0) {
        ESP_LOGE(TAG, "Rename %s → %s failed", tmp_path, queue->index_path);
        remove(tmp_path);
        return QUEUE_STORE_ERR_IO;
    }

    ESP_LOGI(TAG, "Queue flushed to %s (%d entries, %zu bytes)",
             queue->index_path, queue->count, len);
    return QUEUE_STORE_OK;
}

/* ── Init / deinit ───────────────────────────────────────────────────── */

queue_index_t *queue_store_init(const char *base_path,
                                queue_store_err_t *out_err)
{
    if (out_err) *out_err = QUEUE_STORE_OK;

    /* Allocate handle */
    queue_index_t *queue = calloc(1, sizeof(*queue));
    if (!queue) {
        if (out_err) *out_err = QUEUE_STORE_ERR_IO;
        return NULL;
    }

    queue->capacity = QUEUE_MAX_ENTRIES;
    queue->count    = 0;
    queue->entries  = calloc(QUEUE_MAX_ENTRIES, sizeof(queue_entry_t));
    if (!queue->entries) {
        free(queue);
        if (out_err) *out_err = QUEUE_STORE_ERR_IO;
        return NULL;
    }

    /* Build the index.json path */
    if (!base_path || base_path[0] == '\0') {
        safe_copy(queue->index_path,
                  SD_APP_ROOT QUEUE_INDEX_PATH,
                  sizeof(queue->index_path));
    } else {
        snprintf(queue->index_path, sizeof(queue->index_path),
                 "%s" QUEUE_INDEX_PATH, base_path);
    }

    /* Load from disk */
    FILE *f = fopen(queue->index_path, "r");
    if (!f) {
        ESP_LOGI(TAG, "No existing queue file at %s — starting empty",
                 queue->index_path);
        return queue;  /* fresh queue */
    }

    /* Read file into buffer */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fclose(f);
        ESP_LOGI(TAG, "Empty queue file — starting fresh");
        return queue;
    }

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        if (out_err) *out_err = QUEUE_STORE_ERR_IO;
        ESP_LOGE(TAG, "OOM reading queue — starting empty");
        return queue;
    }

    size_t read = fread(content, 1, size, f);
    fclose(f);
    content[read] = '\0';

    int parsed = queue_deserialize(content, queue->entries,
                                   queue->capacity, &queue->count);
    free(content);

    if (parsed < 0) {
        ESP_LOGW(TAG, "Queue file parse error — starting empty");
        queue->count = 0;
        if (out_err) *out_err = QUEUE_STORE_ERR_PARSE;
    } else {
        ESP_LOGI(TAG, "Loaded %d queue entries from %s",
                 queue->count, queue->index_path);
    }

    /* ── Crash recovery: uploading → pending ─────────────────────── */
    int recovered = queue_recover_uploading(queue->entries, queue->count);
    if (recovered > 0) {
        /* Persist the recovery immediately */
        queue_store_flush(queue);
    }

    return queue;
}

void queue_store_deinit(queue_index_t *queue)
{
    if (!queue) return;
    free(queue->entries);
    free(queue);
}

/* ── Enqueue ─────────────────────────────────────────────────────────── */

queue_store_err_t queue_store_enqueue(queue_index_t *queue,
                                      const char *id,
                                      const char *file_path,
                                      uint32_t duration_ms,
                                      uint32_t size)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (!id || !file_path) return QUEUE_STORE_ERR_IO;

    if (queue->count >= queue->capacity) {
        ESP_LOGE(TAG, "Queue full (%d entries) — cannot enqueue %s",
                 queue->capacity, id);
        return QUEUE_STORE_ERR_FULL;
    }

    queue_entry_t *e = &queue->entries[queue->count];
    memset(e, 0, sizeof(*e));
    safe_copy(e->id, id, sizeof(e->id));
    safe_copy(e->file, file_path, sizeof(e->file));
    e->state              = QUEUE_STATE_PENDING;
    e->duration_ms        = duration_ms;
    e->size               = size;
    e->attempts           = 0;
    e->telegram_message_id = 0;

    queue->count++;
    ESP_LOGI(TAG, "Enqueued %s (%" PRIu32 " bytes, %" PRIu32 " ms)",
             id, size, duration_ms);

    return queue_store_flush(queue);
}

/* ── Query / lookup ──────────────────────────────────────────────────── */

queue_entry_t *queue_store_get_next_pending(queue_index_t *queue)
{
    if (!queue) return NULL;
    for (int i = 0; i < queue->count; i++) {
        if (queue->entries[i].state == QUEUE_STATE_PENDING) {
            return &queue->entries[i];
        }
    }
    return NULL;
}

/* ── State transitions ───────────────────────────────────────────────── */

static int find_entry_index(const queue_index_t *queue,
                            const queue_entry_t *entry)
{
    if (!entry) return -1;
    for (int i = 0; i < queue->count; i++) {
        if (&queue->entries[i] == entry) {
            return i;
        }
    }
    return -1;
}

queue_store_err_t queue_store_mark_uploading(queue_index_t *queue,
                                             queue_entry_t *entry)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (find_entry_index(queue, entry) < 0) return QUEUE_STORE_ERR_NOT_FOUND;

    if (entry->state != QUEUE_STATE_PENDING) {
        ESP_LOGW(TAG, "mark_uploading %s from %s (expected pending)",
                 entry->id, queue_state_str(entry->state));
    }
    entry->state = QUEUE_STATE_UPLOADING;
    return queue_store_flush(queue);
}

queue_store_err_t queue_store_mark_sent(queue_index_t *queue,
                                        queue_entry_t *entry,
                                        int telegram_message_id)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (find_entry_index(queue, entry) < 0) return QUEUE_STORE_ERR_NOT_FOUND;

    entry->state = QUEUE_STATE_SENT;
    entry->telegram_message_id = telegram_message_id;
    ESP_LOGI(TAG, "Marked %s as sent (msg_id=%d)", entry->id, telegram_message_id);
    return queue_store_flush(queue);
}

queue_store_err_t queue_store_mark_failed(queue_index_t *queue,
                                          queue_entry_t *entry)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (find_entry_index(queue, entry) < 0) return QUEUE_STORE_ERR_NOT_FOUND;

    entry->state = QUEUE_STATE_FAILED;
    ESP_LOGW(TAG, "Marked %s as failed (attempt %d)", entry->id, entry->attempts);
    return queue_store_flush(queue);
}

queue_store_err_t queue_store_revert_to_pending(queue_index_t *queue,
                                                queue_entry_t *entry,
                                                int new_attempts)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (find_entry_index(queue, entry) < 0) return QUEUE_STORE_ERR_NOT_FOUND;

    entry->state = QUEUE_STATE_PENDING;
    entry->attempts = new_attempts;
    ESP_LOGI(TAG, "Reverted %s to pending (attempt %d)",
             entry->id, new_attempts);
    return queue_store_flush(queue);
}

queue_store_err_t queue_store_reset_for_send_all(queue_index_t *queue,
                                                 queue_entry_t *entry)
{
    if (!queue) return QUEUE_STORE_ERR_NOT_INITIALIZED;
    if (find_entry_index(queue, entry) < 0) return QUEUE_STORE_ERR_NOT_FOUND;

    if (entry->state != QUEUE_STATE_FAILED) {
        ESP_LOGW(TAG, "reset_for_send_all %s from %s (expected failed)",
                 entry->id, queue_state_str(entry->state));
    }
    entry->state = QUEUE_STATE_PENDING;
    entry->attempts = 0;
    ESP_LOGI(TAG, "Reset %s to pending for manual Send All", entry->id);
    return queue_store_flush(queue);
}

/* ── Count helpers (for UI badge) ────────────────────────────────────── */

int queue_store_count_pending_failed(const queue_index_t *queue)
{
    return queue_store_count_pending(queue) + queue_store_count_failed(queue);
}

int queue_store_count_pending(const queue_index_t *queue)
{
    if (!queue) return 0;
    int count = 0;
    for (int i = 0; i < queue->count; i++) {
        if (queue->entries[i].state == QUEUE_STATE_PENDING) {
            count++;
        }
    }
    return count;
}

int queue_store_count_failed(const queue_index_t *queue)
{
    if (!queue) return 0;
    int count = 0;
    for (int i = 0; i < queue->count; i++) {
        if (queue->entries[i].state == QUEUE_STATE_FAILED) {
            count++;
        }
    }
    return count;
}

int queue_store_entry_count(const queue_index_t *queue)
{
    if (!queue) return 0;
    return queue->count;
}

const queue_entry_t *queue_store_get_entries(const queue_index_t *queue,
                                             int *out_count)
{
    if (!queue) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (out_count) *out_count = queue->count;
    return queue->entries;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Pure-logic functions (no filesystem, no ESP-IDF, unit-testable)
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── JSON escape helper ──────────────────────────────────────────────── */

static int pure_json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t pos = 0;
    if (!src) {
        if (dst_size >= 5) { memcpy(dst, "null", 5); return 4; }
        return 0;
    }
    if (dst_size < 3) return 0;
    dst[pos++] = '"';
    for (const char *p = src; *p && pos < dst_size - 3; p++) {
        if (*p == '"' || *p == '\\') {
            if (pos + 2 >= dst_size - 1) break;
            dst[pos++] = '\\';
            dst[pos++] = *p;
        } else {
            dst[pos++] = *p;
        }
    }
    if (pos >= dst_size - 1) { dst[0] = '\0'; return 0; }
    dst[pos++] = '"';
    dst[pos] = '\0';
    return (int)pos;
}

/* ── queue_serialize ─────────────────────────────────────────────────── */

size_t queue_serialize(const queue_entry_t *entries, int count,
                       char *buf, size_t buf_size)
{
    if (!entries || count < 0 || !buf || buf_size == 0) return 0;

    size_t pos = 0;
    int n;

    n = snprintf(buf + pos, buf_size - pos, "[\n");
    if (n < 0 || (size_t)n >= buf_size - pos) { buf[0] = '\0'; return 0; }
    pos += n;

    for (int i = 0; i < count; i++) {
        const queue_entry_t *e = &entries[i];
        char escaped_id[QUEUE_ID_MAX * 2 + 4];
        char escaped_file[QUEUE_FILE_MAX * 2 + 4];
        pure_json_escape(escaped_id, sizeof(escaped_id), e->id);
        pure_json_escape(escaped_file, sizeof(escaped_file), e->file);

        n = snprintf(buf + pos, buf_size - pos,
                     "  {\n"
                     "    \"id\": %s,\n"
                     "    \"file\": %s,\n"
                     "    \"state\": \"%s\",\n"
                     "    \"duration_ms\": %" PRIu32 ",\n"
                     "    \"size\": %" PRIu32 ",\n"
                     "    \"attempts\": %d,\n"
                     "    \"telegram_message_id\": %d\n"
                     "  }%s\n",
                     escaped_id,
                     escaped_file,
                     queue_state_str(e->state),
                     e->duration_ms,
                     e->size,
                     e->attempts,
                     e->telegram_message_id,
                     (i < count - 1) ? "," : "");
        if (n < 0 || (size_t)n >= buf_size - pos) { buf[0] = '\0'; return 0; }
        pos += n;
    }

    n = snprintf(buf + pos, buf_size - pos, "]\n");
    if (n < 0 || (size_t)n >= buf_size - pos) { buf[0] = '\0'; return 0; }
    pos += n;

    return pos;
}

/* ── Minimal JSON parser helpers ─────────────────────────────────────── */

static const char *pure_skip_ws(const char *s)
{
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s;
}

static const char *pure_get_string(const char *s, char *dst, size_t dst_size)
{
    if (!s || *s != '"') return NULL;
    s++;
    size_t pos = 0;
    while (*s && *s != '"') {
        if (*s == '\\' && *(s + 1)) {
            s++;
        }
        if (pos < dst_size - 1) {
            dst[pos++] = *s;
        }
        if (*s) s++;
    }
    dst[pos] = '\0';
    if (*s == '"') s++;
    return s;
}

static const char *pure_skip_value(const char *s)
{
    if (!s) return NULL;
    s = pure_skip_ws(s);
    if (!*s) return s;
    if (*s == '"') {
        s++;
        while (*s && *s != '"') {
            if (*s == '\\' && *(s + 1)) s++;
            s++;
        }
        if (*s == '"') s++;
        return s;
    }
    if (*s == '-' || isdigit((unsigned char)*s)) {
        while (*s && (isdigit((unsigned char)*s) || *s == '-' || *s == '.')) s++;
        return s;
    }
    if (*s == '{') {
        int depth = 1;
        s++;
        while (*s && depth > 0) {
            if (*s == '{') depth++;
            if (*s == '}') depth--;
            s++;
        }
        return s;
    }
    if (*s == '[') {
        int depth = 1;
        s++;
        while (*s && depth > 0) {
            if (*s == '[') depth++;
            if (*s == ']') depth--;
            s++;
        }
        return s;
    }
    while (*s && isalpha((unsigned char)*s)) s++;
    return s;
}

static const char *pure_get_int(const char *s, int *out)
{
    if (!s || !out) return NULL;
    s = pure_skip_ws(s);
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s) return NULL;
    *out = (int)val;
    return end;
}

static const char *pure_get_uint32(const char *s, uint32_t *out)
{
    if (!s || !out) return NULL;
    s = pure_skip_ws(s);
    char *end = NULL;
    unsigned long val = strtoul(s, &end, 10);
    if (end == s) return NULL;
    *out = (uint32_t)val;
    return end;
}

/* ── queue_deserialize ───────────────────────────────────────────────── */

int queue_deserialize(const char *content,
                      queue_entry_t *entries, int capacity,
                      int *out_count)
{
    if (out_count) *out_count = 0;
    if (!content || !entries || capacity < 0) return -1;

    int count = 0;
    const char *p = pure_skip_ws(content);
    if (!p || *p != '[') return -1;
    p++;
    p = pure_skip_ws(p);

    while (p && *p && *p != ']') {
        if (count >= capacity) return -1;

        p = pure_skip_ws(p);
        if (*p == ',') { p++; p = pure_skip_ws(p); }
        if (*p == ']') break;
        if (*p != '{') return -1;

        queue_entry_t *e = &entries[count];
        memset(e, 0, sizeof(*e));

        p = pure_skip_ws(p + 1);

        while (p && *p && *p != '}') {
            p = pure_skip_ws(p);
            if (*p == ',') { p++; p = pure_skip_ws(p); }
            if (*p == '}') break;

            char key[64];
            p = pure_get_string(p, key, sizeof(key));
            if (!p) return -1;

            p = pure_skip_ws(p);
            if (!p || *p != ':') return -1;
            p = pure_skip_ws(p + 1);

            if (strcmp(key, "id") == 0) {
                p = pure_get_string(p, e->id, sizeof(e->id));
            } else if (strcmp(key, "file") == 0) {
                p = pure_get_string(p, e->file, sizeof(e->file));
            } else if (strcmp(key, "state") == 0) {
                char state_str[32];
                p = pure_get_string(p, state_str, sizeof(state_str));
                e->state = queue_state_from_str(state_str);
            } else if (strcmp(key, "duration_ms") == 0) {
                p = pure_get_uint32(p, &e->duration_ms);
            } else if (strcmp(key, "size") == 0) {
                p = pure_get_uint32(p, &e->size);
            } else if (strcmp(key, "attempts") == 0) {
                p = pure_get_int(p, &e->attempts);
            } else if (strcmp(key, "telegram_message_id") == 0) {
                p = pure_get_int(p, &e->telegram_message_id);
            } else {
                p = pure_skip_value(p);
            }
            if (!p) return -1;
        }

        if (p && *p == '}') {
            p++;
            count++;
        }
    }

    if (out_count) *out_count = count;
    return count;
}

/* ── queue_recover_uploading ─────────────────────────────────────────── */

int queue_recover_uploading(queue_entry_t *entries, int count)
{
    if (!entries || count < 0) return 0;
    int recovered = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].state == QUEUE_STATE_UPLOADING) {
            entries[i].state = QUEUE_STATE_PENDING;
            entries[i].attempts = 0;
            recovered++;
        }
    }
    return recovered;
}
