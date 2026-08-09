/** @file wav_to_mp3.c
 * @brief WAV → MP3 conversion using the vendored shine encoder.
 */

#include "wav_to_mp3.h"
#include "wav_writer.h"
#include "layer3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Fixed encode bitrate — plenty for voice at 16kHz mono, smaller/faster
 * than the previous 96kbps.
 * Must stay under ~116kbps: at 16kHz the shine encoder only has one
 * granule/frame (MPEG2), and above that the per-granule bit budget
 * exceeds the 12-bit part2_3_length field's 4095-bit max, so encoded
 * data silently comes up short of what each frame's header declares
 * (breaks frame sync for every downstream decoder, incl. Telegram). */
#define WAV_TO_MP3_BITRATE_KBPS  64

/** Trailing audio dropped before encoding — the stop button's click/pop
 * lands in the last ~150ms of every recording. */
#define WAV_TO_MP3_TRIM_TAIL_MS  150

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

    uint32_t total_frames = hdr.data_size / (hdr.num_channels * sizeof(int16_t));
    uint32_t trim_frames = hdr.sample_rate * WAV_TO_MP3_TRIM_TAIL_MS / 1000;
    /* If trimming would swallow the whole clip, keep it whole instead —
     * an empty MP3 is worse than one with the click still in it. */
    uint32_t keep_frames = (trim_frames < total_frames) ? total_frames - trim_frames : total_frames;
    uint32_t frames_left = keep_frames;

    int samples_per_pass = shine_samples_per_pass(s);
    int frame_samples = samples_per_pass * hdr.num_channels;
    int16_t *frame = malloc(frame_samples * sizeof(int16_t));
    bool ok = (frame != NULL);

    while (ok && frames_left > 0) {
        size_t want_frames = (frames_left < (uint32_t)samples_per_pass)
                              ? frames_left : (uint32_t)samples_per_pass;
        size_t nread = fread(frame, sizeof(int16_t),
                             want_frames * hdr.num_channels, in);
        if (nread == 0) break;
        frames_left -= nread / hdr.num_channels;

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
