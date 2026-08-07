/** @file wav_writer.c
 * @brief WAV file writer — placeholder header → append PCM → finalize → fsync.
 */

#include "wav_writer.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"

static const char *TAG = "wav_writer";

/* ── Internal struct ─────────────────────────────────────────────────── */

struct wav_writer_s {
    FILE     *file;
    char      path[256];
    uint16_t  num_channels;
    uint32_t  sample_rate;
    uint16_t  bits_per_sample;
    uint32_t  data_size;        /**< Bytes of PCM written so far          */
    bool      finalized;        /**< true after wav_writer_finalize()      */
};

/* ── Pure-logic header fill ──────────────────────────────────────────── */

void wav_header_fill(wav_header_t *hdr,
                     uint16_t num_channels,
                     uint32_t sample_rate,
                     uint16_t bits_per_sample,
                     uint32_t data_size)
{
    if (!hdr) return;

    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint16_t block_align      = num_channels * bytes_per_sample;
    uint32_t byte_rate        = sample_rate * block_align;
    uint32_t fmt_chunk_size   = 16;  /* PCM format                          */
    uint32_t file_size        = 4              /* "WAVE"                   */
                              + (8 + fmt_chunk_size) /* fmt chunk          */
                              + (8 + data_size);     /* data chunk         */

    memset(hdr, 0, sizeof(*hdr));

    /* RIFF chunk descriptor */
    memcpy(hdr->riff_id, "RIFF", 4);
    hdr->file_size = file_size;
    memcpy(hdr->wave_id, "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(hdr->fmt_id, "fmt ", 4);
    hdr->fmt_size        = fmt_chunk_size;
    hdr->audio_format    = 1;               /* PCM                        */
    hdr->num_channels    = num_channels;
    hdr->sample_rate     = sample_rate;
    hdr->byte_rate       = byte_rate;
    hdr->block_align     = block_align;
    hdr->bits_per_sample = bits_per_sample;

    /* data sub-chunk */
    memcpy(hdr->data_id, "data", 4);
    hdr->data_size = data_size;
}

/* ── Writer open ─────────────────────────────────────────────────────── */

wav_writer_t *wav_writer_open(const char *path,
                              uint16_t num_channels,
                              uint32_t sample_rate,
                              uint16_t bits_per_sample)
{
    if (!path || num_channels == 0 || sample_rate == 0 || bits_per_sample == 0) {
        return NULL;
    }

    wav_writer_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->file = fopen(path, "wb");
    if (!w->file) {
        ESP_LOGE(TAG, "Cannot create WAV file: %s", path);
        free(w);
        return NULL;
    }

    /* Store params */
    strncpy(w->path, path, sizeof(w->path) - 1);
    w->path[sizeof(w->path) - 1] = '\0';
    w->num_channels    = num_channels;
    w->sample_rate     = sample_rate;
    w->bits_per_sample = bits_per_sample;
    w->data_size       = 0;
    w->finalized       = false;

    /* Write placeholder header (data_size = 0) */
    wav_header_t hdr;
    wav_header_fill(&hdr, num_channels, sample_rate, bits_per_sample, 0);
    size_t written = fwrite(&hdr, 1, sizeof(hdr), w->file);
    if (written != sizeof(hdr)) {
        ESP_LOGE(TAG, "Failed to write WAV header: %s", path);
        fclose(w->file);
        free(w);
        return NULL;
    }
    if (fflush(w->file) != 0) {
        ESP_LOGE(TAG, "Initial flush failed for %s", w->path);
        fclose(w->file);
        free(w);
        return NULL;
    }

    ESP_LOGI(TAG, "WAV opened: %s (%d Hz, %d ch, %d bit)",
             w->path, (int)sample_rate, (int)num_channels, (int)bits_per_sample);

    return w;
}

/* ── Writer write ────────────────────────────────────────────────────── */

void wav_writer_write(wav_writer_t *w, const int16_t *samples, size_t count)
{
    if (!w || !w->file || !samples || count == 0) return;

    size_t bytes = count * sizeof(int16_t);
    size_t written = fwrite(samples, 1, bytes, w->file);
    if (written != bytes) {
        ESP_LOGE(TAG, "Short write to %s: %zu / %zu", w->path, written, bytes);
    }
    w->data_size += (uint32_t)written;
}

/* ── Writer finalize ─────────────────────────────────────────────────── */

bool wav_writer_finalize(wav_writer_t *w)
{
    if (!w || !w->file) return false;
    if (w->finalized) return true;  /* idempotent                          */

    /* Flush any buffered writes first */
    if (fflush(w->file) != 0) {
        ESP_LOGE(TAG, "Final flush failed for %s", w->path);
        return false;
    }

    /* Seek to start and write the real header */
    int seek_ret = fseek(w->file, 0, SEEK_SET);
    if (seek_ret != 0) {
        ESP_LOGE(TAG, "fseek to 0 failed for %s", w->path);
        return false;
    }

    wav_header_t hdr;
    wav_header_fill(&hdr, w->num_channels, w->sample_rate,
                    w->bits_per_sample, w->data_size);

    size_t written = fwrite(&hdr, 1, sizeof(hdr), w->file);
    if (written != sizeof(hdr)) {
        ESP_LOGE(TAG, "Header rewrite failed for %s", w->path);
        return false;
    }

    /* Flush libc buffers, then fsync the underlying fd */
    if (fflush(w->file) != 0) {
        ESP_LOGE(TAG, "Final header flush failed for %s", w->path);
        return false;
    }

    int fd = fileno(w->file);
    if (fd < 0 || fsync(fd) != 0) {
        ESP_LOGE(TAG, "fsync failed for %s", w->path);
        return false;
    }

    w->finalized = true;
    ESP_LOGI(TAG, "WAV finalized: %s (data_size=%" PRIu32 ")", w->path, w->data_size);
    return true;
}

/* ── Writer close ────────────────────────────────────────────────────── */

void wav_writer_close(wav_writer_t *w)
{
    if (!w) return;

    /* Best-effort finalize if the caller never did */
    if (!w->finalized && w->file) {
        ESP_LOGW(TAG, "Closing unfinalized WAV: %s — finalizing now", w->path);
        wav_writer_finalize(w);
    }

    if (w->file) {
        fclose(w->file);
        w->file = NULL;
    }

    ESP_LOGI(TAG, "WAV closed: %s", w->path);
    free(w);
}

/* ── Query ───────────────────────────────────────────────────────────── */

uint32_t wav_writer_bytes_written(const wav_writer_t *w)
{
    if (!w) return 0;
    return w->data_size;
}

const char *wav_writer_path(const wav_writer_t *w)
{
    if (!w) return NULL;
    return w->path;
}
