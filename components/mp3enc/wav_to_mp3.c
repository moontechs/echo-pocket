/** @file wav_to_mp3.c
 * @brief WAV → MP3 conversion using the vendored shine encoder.
 */

#include "wav_to_mp3.h"
#include "wav_writer.h"
#include "layer3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Fixed encode bitrate — adequate for voice, keeps files small. */
#define WAV_TO_MP3_BITRATE_KBPS  64

static bool read_wav_header(FILE *fp, wav_header_t *hdr)
{
    if (fread(hdr, 1, sizeof(*hdr), fp) != sizeof(*hdr)) return false;

    if (memcmp(hdr->riff_id, "RIFF", 4) != 0) return false;
    if (memcmp(hdr->wave_id, "WAVE", 4) != 0) return false;
    if (memcmp(hdr->data_id, "data", 4) != 0) return false;
    if (hdr->audio_format != 1) return false;      /* PCM only */
    if (hdr->bits_per_sample != 16) return false;   /* s16 only */
    if (hdr->num_channels != 1 && hdr->num_channels != 2) return false;

    return true;
}

bool wav_to_mp3(const char *wav_path, const char *mp3_path)
{
    if (!wav_path || !mp3_path) return false;

    FILE *in = fopen(wav_path, "rb");
    if (!in) return false;

    wav_header_t hdr;
    if (!read_wav_header(in, &hdr)) {
        fclose(in);
        return false;
    }

    int bitrate_idx = shine_find_bitrate_index(WAV_TO_MP3_BITRATE_KBPS, MPEG_II);
    int mpeg_version = shine_check_config((int)hdr.sample_rate, WAV_TO_MP3_BITRATE_KBPS);
    if (bitrate_idx < 0 || mpeg_version < 0) {
        fclose(in);
        return false;
    }

    shine_config_t config;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.wave.channels   = (hdr.num_channels == 1) ? PCM_MONO : PCM_STEREO;
    config.wave.samplerate = (int)hdr.sample_rate;
    config.mpeg.mode       = (hdr.num_channels == 1) ? MONO : JOINT_STEREO;
    config.mpeg.bitr       = WAV_TO_MP3_BITRATE_KBPS;

    shine_t s = shine_initialise(&config);
    if (!s) {
        fclose(in);
        return false;
    }

    FILE *out = fopen(mp3_path, "wb");
    if (!out) {
        shine_close(s);
        fclose(in);
        return false;
    }

    int samples_per_pass = shine_samples_per_pass(s);
    int frame_samples = samples_per_pass * hdr.num_channels;
    int16_t *frame = malloc(frame_samples * sizeof(int16_t));
    bool ok = (frame != NULL);

    while (ok) {
        size_t nread = fread(frame, sizeof(int16_t), frame_samples, in);
        if (nread == 0) break;

        /* Pad a short final frame with silence — shine requires exactly
         * samples_per_pass samples per call. */
        if ((int)nread < frame_samples) {
            memset(frame + nread, 0, (frame_samples - nread) * sizeof(int16_t));
        }

        int written = 0;
        unsigned char *data = shine_encode_buffer_interleaved(s, frame, &written);
        if (written > 0 && data) {
            if (fwrite(data, 1, written, out) != (size_t)written) {
                ok = false;
                break;
            }
        }

        if ((int)nread < frame_samples) break; /* consumed the last (padded) frame */
    }

    if (ok) {
        int written = 0;
        unsigned char *data = shine_flush(s, &written);
        if (written > 0 && data) {
            ok = (fwrite(data, 1, written, out) == (size_t)written);
        }
    }

    free(frame);
    shine_close(s);
    fclose(out);
    fclose(in);

    if (!ok) remove(mp3_path);
    return ok;
}
