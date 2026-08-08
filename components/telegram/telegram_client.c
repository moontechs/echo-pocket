/** @file telegram_client.c
 * @brief Telegram Bot API client implementation for echo-pocket (ESP-IDF).
 *
 * Uses esp_http_client for sendDocument (multipart/form-data) and getMe,
 * streaming WAV files directly from SD without buffering the full file.
 *
 * Dependencies:
 *   - esp_http_client  (IDF managed component)
 *   - cJSON            (bundled with IDF or managed component)
 *   - storage/config.h (RecorderConfig)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "telegram_client.h"

static const char *TAG = "tg_client";

/* ── Internal state ──────────────────────────────────────────────────── */

static char s_bot_token[CONFIG_MAX_BOT_TOKEN];
static char s_api_url_base[CONFIG_MAX_BOT_TOKEN + 64]; /* "https://.../bot<token>" */
static bool s_initialised = false;

/* ── Buffer sizes ─────────────────────────────────────────────────────── */

/** Max HTTP response body we read for JSON parsing (getMe ~256B, sendDocument ~512B). */
#define TG_RESPONSE_BUF_SIZE   2048

/** Chunk size for streaming file from SD — small enough to not blow RAM. */
#define TG_UPLOAD_CHUNK_SIZE   8192

/* ── Helpers ──────────────────────────────────────────────────────────── */

/**
 * Parse a Telegram API JSON response of the form:
 *   {"ok":true,"result":{...}}  or  {"ok":false,"description":"..."}
 *
 * On ok:true, optionally extract "result"."message_id" as int.
 * On ok:false, populate out_error with the description string (if available).
 *
 * @param body         Null-terminated JSON body.
 * @param out_message_id  If non-NULL, set to message_id on ok:true.
 * @param out_error    If non-NULL and ok:false, set to description.
 * @param out_error_size  Size of out_error buffer.
 * @return true if ok:true, false otherwise.
 */
static bool tg_parse_response(const char *body,
                               int *out_message_id,
                               char *out_error, size_t out_error_size)
{
    if (!body) return false;

    cJSON *root = cJSON_Parse(body);
    if (!root) return false;

    bool ok = false;
    cJSON *ok_item = cJSON_GetObjectItem(root, "ok");
    if (ok_item && cJSON_IsBool(ok_item)) {
        ok = cJSON_IsTrue(ok_item);
    }

    if (ok) {
        if (out_message_id) {
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result) {
                cJSON *msg_id = cJSON_GetObjectItem(result, "message_id");
                if (msg_id && cJSON_IsNumber(msg_id)) {
                    *out_message_id = msg_id->valueint;
                }
            }
        }
    } else {
        if (out_error && out_error_size > 0) {
            out_error[0] = '\0';
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            if (desc && cJSON_IsString(desc)) {
                strncpy(out_error, desc->valuestring, out_error_size - 1);
                out_error[out_error_size - 1] = '\0';
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

/**
 * Build the full API URL:  https://api.telegram.org/bot<token>/<method>
 *
 * @param method  e.g. "getMe" or "sendDocument".
 * @param buf     Output buffer.
 * @param size    Buffer size.
 */
static void tg_build_url(const char *method, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s", s_api_url_base, method);
}

/* ── HTTP response reader (accumulates body into a buffer) ───────────── */

typedef struct {
    char   *buf;
    size_t  capacity;
    size_t  written;
} tg_response_ctx_t;

static esp_err_t tg_http_event_handler(esp_http_client_event_t *evt)
{
    tg_response_ctx_t *ctx = (tg_response_ctx_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx && ctx->buf && ctx->written < ctx->capacity) {
            size_t remaining = ctx->capacity - ctx->written - 1; /* reserve NUL */
            size_t to_copy = (evt->data_len < remaining) ? evt->data_len : remaining;
            memcpy(ctx->buf + ctx->written, evt->data, to_copy);
            ctx->written += to_copy;
            ctx->buf[ctx->written] = '\0';
        }
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────── */

telegram_err_t telegram_client_init(const char *bot_token)
{
    if (!bot_token || bot_token[0] == '\0') {
        ESP_LOGE(TAG, "init: null or empty bot_token");
        return TELEGRAM_ERR_NULL_PARAM;
    }

    strncpy(s_bot_token, bot_token, sizeof(s_bot_token) - 1);
    s_bot_token[sizeof(s_bot_token) - 1] = '\0';

    snprintf(s_api_url_base, sizeof(s_api_url_base),
             "%s%s", TELEGRAM_API_PREFIX, s_bot_token);

    s_initialised = true;
    ESP_LOGI(TAG, "client initialised (token starts with %c)",
             s_bot_token[0]);
    return TELEGRAM_OK;
}

void telegram_client_deinit(void)
{
    s_initialised = false;
    s_bot_token[0] = '\0';
    s_api_url_base[0] = '\0';
}

telegram_err_t telegram_client_get_me(char *out_bot_username)
{
    if (!s_initialised) {
        return TELEGRAM_ERR_NULL_PARAM;
    }

    char url[256];
    tg_build_url("getMe", url, sizeof(url));

    char response[TG_RESPONSE_BUF_SIZE] = {0};
    tg_response_ctx_t ctx = { response, sizeof(response), 0 };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = tg_http_event_handler,
        .user_data = &ctx,
        .timeout_ms = 15000,
        .buffer_size = 512,
        .disable_auto_redirect = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "getMe: failed to init HTTP client");
        return TELEGRAM_ERR_OOM;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "getMe: HTTP request failed: %d", err);
        return TELEGRAM_ERR_CONNECT;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "getMe: HTTP %d, body: %s", status, response);
        return TELEGRAM_ERR_HTTP;
    }

    /* Parse response */
    char api_error[256] = {0};
    if (!tg_parse_response(response, NULL, api_error, sizeof(api_error))) {
        ESP_LOGE(TAG, "getMe: API error: %s", api_error);
        return TELEGRAM_ERR_API;
    }

    /* Extract bot username from result.username */
    if (out_bot_username) {
        out_bot_username[0] = '\0';
        cJSON *root = cJSON_Parse(response);
        if (root) {
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result) {
                cJSON *username = cJSON_GetObjectItem(result, "username");
                if (username && cJSON_IsString(username)) {
                    strncpy(out_bot_username, username->valuestring, 63);
                    out_bot_username[63] = '\0';
                }
            }
            cJSON_Delete(root);
        }
    }

    ESP_LOGI(TAG, "getMe: OK, username=%s",
             out_bot_username ? out_bot_username : "(not requested)");
    return TELEGRAM_OK;
}

/* ── Shared multipart/form-data file sender ──────────────────────────── */

/**
 * Send a file to a Telegram Bot API method as multipart/form-data, with an
 * optional caption. Shared by sendDocument and sendAudio — they differ only
 * in method name, form field name, MIME type, and uploaded filename.
 *
 * Streams the file from SD via esp_http_client_write() rather than
 * buffering it in RAM.
 */
static telegram_err_t tg_send_multipart_file(const char *method,
                                              const char *field_name,
                                              const char *content_type_value,
                                              const char *upload_filename,
                                              const char *chat_id,
                                              const char *file_path,
                                              const char *caption,
                                              int *out_message_id)
{
    if (!s_initialised) {
        return TELEGRAM_ERR_NULL_PARAM;
    }
    if (!chat_id || !file_path) {
        return TELEGRAM_ERR_NULL_PARAM;
    }

    /* ── Check file exists and is within size limit ─────────────────── */
    struct stat st;
    if (stat(file_path, &st) != 0) {
        ESP_LOGE(TAG, "%s: file not found: %s", method, file_path);
        return TELEGRAM_ERR_FILE_NOT_FOUND;
    }

    if ((uint64_t)st.st_size > TELEGRAM_MAX_UPLOAD_BYTES) {
        ESP_LOGE(TAG, "%s: file too large: %lld bytes (max %d)",
                 method, (long long)st.st_size, TELEGRAM_MAX_UPLOAD_BYTES);
        return TELEGRAM_ERR_FILE_TOO_LARGE;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "%s: fopen failed: %s", method, file_path);
        return TELEGRAM_ERR_FILE_NOT_FOUND;
    }

    /* ── Build multipart form body ────────────────────────────────────
     *
     * Telegram multipart format:
     *   --BOUNDARY\r\n
     *   Content-Disposition: form-data; name="chat_id"\r\n\r\n
     *   <chat_id>\r\n
     *   --BOUNDARY\r\n
     *   Content-Disposition: form-data; name="caption"\r\n\r\n
     *   <caption>\r\n
     *   --BOUNDARY\r\n
     *   Content-Disposition: form-data; name="<field_name>"; filename="<upload_filename>"\r\n
     *   Content-Type: <content_type_value>\r\n\r\n
     *   <file data>\r\n
     *   --BOUNDARY--\r\n
     *
     * We stream the file data from SD, so we send headers manually
     * via esp_http_client_open + write + close rather than setting
     * post_field.  This lets us stream the file in chunks without
     * buffering it all in RAM.
     */

    static const char *BOUNDARY = "----EchoPocketBoundary20260804";

    /* ── Assemble pre-body headers (chat_id + caption parts) ───────── */
    char pre_body[2048];
    int pre_len;

    if (caption && caption[0]) {
        pre_len = snprintf(pre_body, sizeof(pre_body),
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"%s\"; "
            "filename=\"%s\"\r\n"
            "Content-Type: %s\r\n\r\n",
            BOUNDARY, chat_id,
            BOUNDARY, caption,
            BOUNDARY, field_name, upload_filename, content_type_value);
    } else {
        pre_len = snprintf(pre_body, sizeof(pre_body),
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"%s\"; "
            "filename=\"%s\"\r\n"
            "Content-Type: %s\r\n\r\n",
            BOUNDARY, chat_id,
            BOUNDARY, field_name, upload_filename, content_type_value);
    }

    /* ── Assemble post-body trailer ────────────────────────────────── */
    char trailer[128];
    int trailer_len = snprintf(trailer, sizeof(trailer),
                                "\r\n--%s--\r\n", BOUNDARY);

    /* Total content-length = pre_body + file + trailer */
    size_t content_length = pre_len + st.st_size + trailer_len;

    /* ── Build URL ─────────────────────────────────────────────────── */
    char url[256];
    tg_build_url(method, url, sizeof(url));

    /* Content-Type header with boundary */
    char content_type[128];
    snprintf(content_type, sizeof(content_type),
             "multipart/form-data; boundary=%s", BOUNDARY);

    /* ── Configure and open HTTP client ────────────────────────────── */
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = NULL,  /* We read response via read_response() */
        .timeout_ms = 120000,  /* 2 minutes — enough for 38 MB upload */
        .buffer_size = TG_UPLOAD_CHUNK_SIZE,
        .disable_auto_redirect = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        fclose(fp);
        return TELEGRAM_ERR_OOM;
    }

    /* Streaming manually via esp_http_client_write() below, so no
     * post_field is set — a freshly-initialised client already has
     * post_data == NULL. Do NOT call esp_http_client_set_post_field(client,
     * NULL, 0) here: passing a NULL data pointer makes it actively clear
     * the Content-Type header we just set (its NULL branch calls
     * esp_http_client_set_header(client, "Content-Type", NULL)), which
     * silently dropped our multipart boundary declaration on every upload. */
    esp_http_client_set_header(client, "Content-Type", content_type);

    char content_len_str[32];
    snprintf(content_len_str, sizeof(content_len_str), "%zu", content_length);
    esp_http_client_set_header(client, "Content-Length", content_len_str);

    /* ── Open connection ───────────────────────────────────────────── */
    esp_err_t esp_err = esp_http_client_open(client, content_length);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "%s: open failed: %d", method, esp_err);
        esp_http_client_cleanup(client);
        fclose(fp);
        return TELEGRAM_ERR_CONNECT;
    }

    /* ── Write pre-body (chat_id + caption + document header) ──────── */
    int written = esp_http_client_write(client, pre_body, pre_len);
    if (written != pre_len) {
        ESP_LOGE(TAG, "%s: pre-body write failed: %d/%d",
                 method, written, pre_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(fp);
        return TELEGRAM_ERR_HTTP;
    }

    /* ── Stream file from SD in chunks ─────────────────────────────── */
    /* Heap-allocated (not a stack array): this task's stack is small and
     * shared with call-depth from esp_http_client/mbedTLS, so an 8 KB
     * local here overflowed the stack and corrupted adjacent memory. */
    uint8_t *chunk = malloc(TG_UPLOAD_CHUNK_SIZE);
    if (!chunk) {
        ESP_LOGE(TAG, "%s: chunk buffer alloc failed", method);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(fp);
        return TELEGRAM_ERR_OOM;
    }

    size_t total_file_sent = 0;
    bool stream_ok = true;

    while (!feof(fp)) {
        size_t nread = fread(chunk, 1, TG_UPLOAD_CHUNK_SIZE, fp);
        if (nread == 0) break; /* EOF or error */

        written = esp_http_client_write(client, (const char *)chunk, nread);
        if (written != (int)nread) {
            ESP_LOGE(TAG, "%s: chunk write failed: %d/%zu at offset %zu",
                     method, written, nread, total_file_sent);
            stream_ok = false;
            break;
        }
        total_file_sent += nread;
    }
    free(chunk);
    fclose(fp);

    if (!stream_ok) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return TELEGRAM_ERR_HTTP;
    }

    /* ── Write trailer ─────────────────────────────────────────────── */
    written = esp_http_client_write(client, trailer, trailer_len);
    if (written != trailer_len) {
        ESP_LOGE(TAG, "%s: trailer write failed: %d/%d",
                 method, written, trailer_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return TELEGRAM_ERR_HTTP;
    }

    /* ── Finish request and read response ──────────────────────────── */
    int response_length = esp_http_client_fetch_headers(client);
    if (response_length < 0 && response_length != -ESP_ERR_HTTP_EAGAIN) {
        ESP_LOGE(TAG, "%s: fetch_headers failed: %d", method, response_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return TELEGRAM_ERR_HTTP;
    }

    int status = esp_http_client_get_status_code(client);

    /* Read response body */
    char response[TG_RESPONSE_BUF_SIZE] = {0};
    if (response_length > 0) {
        int to_read = (response_length < (int)(sizeof(response) - 1))
                        ? response_length : (int)(sizeof(response) - 1);
        int bytes_read = esp_http_client_read(client, response, to_read);
        if (bytes_read < 0) {
            ESP_LOGW(TAG, "%s: read returned %d", method, bytes_read);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    /* ── Check HTTP status ─────────────────────────────────────────── */
    if (status != 200) {
        ESP_LOGE(TAG, "%s: HTTP %d, body: %s", method, status, response);
        return TELEGRAM_ERR_HTTP;
    }

    /* ── Parse API response ────────────────────────────────────────── */
    char api_error[256] = {0};
    int msg_id = 0;

    if (!tg_parse_response(response, &msg_id, api_error, sizeof(api_error))) {
        ESP_LOGE(TAG, "%s: API error: %s", method, api_error);
        return TELEGRAM_ERR_API;
    }

    if (out_message_id) {
        *out_message_id = msg_id;
    }

    ESP_LOGI(TAG, "%s: OK, message_id=%d, sent=%zu bytes, file=%s",
             method, msg_id, total_file_sent, file_path);
    return TELEGRAM_OK;
}

telegram_err_t telegram_client_send_document(const char *chat_id,
                                              const char *file_path,
                                              const char *caption,
                                              int *out_message_id)
{
    return tg_send_multipart_file("sendDocument", "document", "audio/wav",
                                   "recording.wav", chat_id, file_path,
                                   caption, out_message_id);
}

telegram_err_t telegram_client_send_audio(const char *chat_id,
                                           const char *file_path,
                                           const char *upload_filename,
                                           int *out_message_id)
{
    return tg_send_multipart_file("sendAudio", "audio", "audio/mpeg",
                                   (upload_filename && upload_filename[0])
                                       ? upload_filename : "voice.mp3",
                                   chat_id, file_path,
                                   NULL, out_message_id);
}

/* ── Fan-out helper ──────────────────────────────────────────────────── */

telegram_err_t telegram_client_send_to_channels(const RecorderConfig *cfg,
                                                 const char *file_path,
                                                 const char *caption,
                                                 int *out_message_id)
{
    if (!cfg || !file_path) {
        return TELEGRAM_ERR_NULL_PARAM;
    }

    telegram_err_t last_err = TELEGRAM_OK;
    int last_msg_id = 0;
    int sent_count = 0;

    if (cfg->send_to_all) {
        /* Fan-out: send to every configured channel */
        for (int i = 0; i < cfg->channel_count; i++) {
            if (cfg->channels[i].id[0] == '\0') continue;

            int msg_id = 0;
            telegram_err_t err = telegram_client_send_document(
                cfg->channels[i].id, file_path, caption, &msg_id);

            if (err == TELEGRAM_OK) {
                sent_count++;
                last_msg_id = msg_id;
            } else {
                ESP_LOGW(TAG, "send_to_channels: channel %s failed: %s",
                         cfg->channels[i].id, telegram_err_str(err));
                last_err = err;
                break; /* Stop on first failure per plan */
            }
        }
    } else {
        /* Single target: active_channel (1-based index) */
        int idx = cfg->active_channel - 1;
        if (idx < 0 || idx >= cfg->channel_count ||
            cfg->channels[idx].id[0] == '\0') {
            ESP_LOGE(TAG, "send_to_channels: invalid active_channel %d",
                     cfg->active_channel);
            return TELEGRAM_ERR_NULL_PARAM;
        }

        telegram_err_t err = telegram_client_send_document(
            cfg->channels[idx].id, file_path, caption, &last_msg_id);

        if (err == TELEGRAM_OK) {
            sent_count++;
        } else {
            last_err = err;
        }
    }

    if (out_message_id) {
        *out_message_id = last_msg_id;
    }

    /* An enabled fan-out with no usable destination is not a successful
     * delivery.  In particular, the upload task must not mark the recording
     * sent (and possibly delete it) without having sent a document. */
    if (sent_count == 0 && last_err == TELEGRAM_OK) {
        ESP_LOGE(TAG, "send_to_channels: no configured target channels");
        return TELEGRAM_ERR_NULL_PARAM;
    }

    ESP_LOGI(TAG, "send_to_channels: %d sent, result=%s",
             sent_count,
             (last_err == TELEGRAM_OK) ? "ok" : telegram_err_str(last_err));

    return (last_err == TELEGRAM_OK) ? TELEGRAM_OK : last_err;
}

telegram_err_t telegram_client_send_audio_to_channels(const RecorderConfig *cfg,
                                                       const char *file_path,
                                                       const char *upload_filename,
                                                       int *out_message_id)
{
    if (!cfg || !file_path) {
        return TELEGRAM_ERR_NULL_PARAM;
    }

    telegram_err_t last_err = TELEGRAM_OK;
    int last_msg_id = 0;
    int sent_count = 0;

    if (cfg->send_to_all) {
        for (int i = 0; i < cfg->channel_count; i++) {
            if (cfg->channels[i].id[0] == '\0') continue;

            int msg_id = 0;
            telegram_err_t err = telegram_client_send_audio(
                cfg->channels[i].id, file_path, upload_filename, &msg_id);

            if (err == TELEGRAM_OK) {
                sent_count++;
                last_msg_id = msg_id;
            } else {
                ESP_LOGW(TAG, "send_audio_to_channels: channel %s failed: %s",
                         cfg->channels[i].id, telegram_err_str(err));
                last_err = err;
                break; /* Stop on first failure per plan */
            }
        }
    } else {
        int idx = cfg->active_channel - 1;
        if (idx < 0 || idx >= cfg->channel_count ||
            cfg->channels[idx].id[0] == '\0') {
            ESP_LOGE(TAG, "send_audio_to_channels: invalid active_channel %d",
                     cfg->active_channel);
            return TELEGRAM_ERR_NULL_PARAM;
        }

        telegram_err_t err = telegram_client_send_audio(
            cfg->channels[idx].id, file_path, upload_filename, &last_msg_id);

        if (err == TELEGRAM_OK) {
            sent_count++;
        } else {
            last_err = err;
        }
    }

    if (out_message_id) {
        *out_message_id = last_msg_id;
    }

    if (sent_count == 0 && last_err == TELEGRAM_OK) {
        ESP_LOGE(TAG, "send_audio_to_channels: no configured target channels");
        return TELEGRAM_ERR_NULL_PARAM;
    }

    ESP_LOGI(TAG, "send_audio_to_channels: %d sent, result=%s",
             sent_count,
             (last_err == TELEGRAM_OK) ? "ok" : telegram_err_str(last_err));

    return (last_err == TELEGRAM_OK) ? TELEGRAM_OK : last_err;
}

/* telegram_format_caption() is implemented in telegram_caption.c
 * (pure function, no ESP-IDF deps — unit-testable in logic_tests) */
