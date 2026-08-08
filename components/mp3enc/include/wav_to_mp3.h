/** @file wav_to_mp3.h
 * @brief Convert a PCM s16 WAV file to MP3 using the vendored shine encoder.
 *
 * Pure file-to-file conversion, no ESP-IDF hardware deps beyond libc —
 * host-testable under logic_tests (see test/test_wav_to_mp3.c).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode a canonical 44-byte-header PCM WAV file to MP3.
 *
 * Reads the WAV header to determine channels/sample rate, encodes at a
 * fixed 64 kbps (adequate for voice), and writes a complete MP3 file.
 *
 * @param wav_path  Path to an existing WAV file (mono or stereo, s16 PCM).
 * @param mp3_path  Path to write the resulting MP3 file (overwritten).
 * @return true on success, false on any read/format/encode/write error.
 */
bool wav_to_mp3(const char *wav_path, const char *mp3_path);

#ifdef __cplusplus
}
#endif
