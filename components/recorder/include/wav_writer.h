/** @file wav_writer.h
 * @brief WAV file writer — placeholder header, append PCM, finalize + fsync.
 *
 * Writes mono or multi-channel PCM s16 WAV.  On open, writes a placeholder
 * RIFF header with zero sizes; on finalize, seeks back and patches the
 * real RIFF/data sizes then fsyncs.  This is the crash-safe pattern from
 * AGENTS.md §7.1: on stop, patch header → fsync → close.
 *
 * The wav_header_fill() function is pure logic (no I/O) and is unit-tested
 * under logic_tests.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── WAV header struct (44 bytes, packed) ───────────────────────────── */

typedef struct __attribute__((packed)) {
    /* RIFF chunk descriptor */
    char     riff_id[4];       /**< "RIFF"                                */
    uint32_t file_size;        /**< 4 + (8 + fmt_size) + (8 + data_size)  */
    char     wave_id[4];       /**< "WAVE"                                */

    /* fmt sub-chunk */
    char     fmt_id[4];        /**< "fmt "                                */
    uint32_t fmt_size;         /**< 16 for PCM                            */
    uint16_t audio_format;     /**< 1 = PCM                               */
    uint16_t num_channels;     /**< 1 = mono, 2 = stereo, …               */
    uint32_t sample_rate;      /**< samples per second                    */
    uint32_t byte_rate;        /**< sample_rate * num_channels * bps/8    */
    uint16_t block_align;      /**< num_channels * bps/8                  */
    uint16_t bits_per_sample;  /**< 16 for s16 PCM                        */

    /* data sub-chunk */
    char     data_id[4];       /**< "data"                                */
    uint32_t data_size;        /**< total bytes of sample data            */
} wav_header_t;

/** Total size of the WAV header in bytes (44). */
#define WAV_HEADER_SIZE  sizeof(wav_header_t)

/* ── Opaque writer handle ───────────────────────────────────────────── */

typedef struct wav_writer_s wav_writer_t;

/* ── Pure-logic header computation (unit-testable) ──────────────────── */

/**
 * @brief Fill a WAV header struct with the correct magic values and
 *        computed fields for the given format and data size.
 *
 * Pure function — no file I/O.  Unit-tested under logic_tests.
 *
 * @param hdr            Output header (must point to valid memory).
 * @param num_channels   Channel count (1 for mono).
 * @param sample_rate    Samples per second (e.g. 16000).
 * @param bits_per_sample Bits per sample (e.g. 16).
 * @param data_size      Total bytes of PCM sample data already written.
 */
void wav_header_fill(wav_header_t *hdr,
                     uint16_t num_channels,
                     uint32_t sample_rate,
                     uint16_t bits_per_sample,
                     uint32_t data_size);

/* ── File operations (require a filesystem — not in logic_tests) ────── */

/**
 * @brief Open a new WAV file at @p path and write a placeholder header.
 *
 * The header has zero data_size / file_size — call wav_writer_finalize()
 * to patch them when recording stops.
 *
 * @param path            Full path to the .wav file.
 * @param num_channels    Channel count.
 * @param sample_rate     Samples per second.
 * @param bits_per_sample Bits per sample.
 * @return  Handle on success, NULL on fopen failure.
 */
wav_writer_t *wav_writer_open(const char *path,
                              uint16_t num_channels,
                              uint32_t sample_rate,
                              uint16_t bits_per_sample);

/**
 * @brief Append PCM s16 samples to the open WAV file.
 *
 * Does NOT update the header — caller must call wav_writer_finalize() later.
 *
 * @param w       Open writer handle.
 * @param samples Interleaved PCM s16 samples.
 * @param count   Number of samples (not bytes, not frames).
 */
void wav_writer_write(wav_writer_t *w, const int16_t *samples, size_t count);

/**
 * @brief Patch the RIFF/data size fields in the header and fsync the file.
 *
 * Seeks back to byte 0, writes the updated header, flushes, and fsyncs.
 * Safe to call multiple times — subsequent calls are no-ops after the first.
 *
 * @return true on success, false on any I/O error.
 */
bool wav_writer_finalize(wav_writer_t *w);

/**
 * @brief Close the WAV file and free the handle.
 *
 * If the file was never finalized, attempts a best-effort finalize first
 * (to avoid leaving a broken placeholder header on disk).
 *
 * Safe to call with NULL.
 */
void wav_writer_close(wav_writer_t *w);

/**
 * @brief Return the number of PCM sample bytes written so far
 *        (data_size, not including the header).
 */
uint32_t wav_writer_bytes_written(const wav_writer_t *w);

/**
 * @brief Return the path the writer was opened with (read-only).
 */
const char *wav_writer_path(const wav_writer_t *w);

#ifdef __cplusplus
}
#endif
