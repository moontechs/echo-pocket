/** @file audio_process.c
 * @brief Audio processing task — ESP-SR AFE between capture and writer.
 *
 * Reads 2-channel stereo frames from the capture ring buffer, runs the
 * ESP-SR AFE (NS + VAD + AGC) to produce clean mono PCM, computes a
 * smoothed voice level, and writes mono output to a downstream ring
 * buffer consumed by the sd_writer_task.
 *
 * When ESP-SR features are disabled in config (noise_suppression=false,
 * voice_detection=false), the AFE is not created and a simple 2ch→mono
 * downmix + energy-based VAD fallback is used instead.
 */

#include "audio_process.h"
#include "audio_capture.h"
#include "voice_level.h"
#include "audio_ringbuf.h"
#include "audio_mono_ringbuf.h"
#include "config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

/* ESP-SR AFE — only available when compiling with ESP-IDF.               */
#if __has_include("esp_afe_sr_iface.h")
#include "esp_afe_sr_iface.h"
#define HAS_ESP_SR  1
#else
#define HAS_ESP_SR  0
#endif

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "audio_proc";

/* ── Static state ────────────────────────────────────────────────────── */

static audio_ringbuf_t       *s_input_rb   = NULL;
static audio_mono_ringbuf_t  *s_output_rb  = NULL;
static TaskHandle_t           s_task       = NULL;
static volatile bool          s_running    = false;

/* Config booleans — snapshotted at init so the task doesn't need to hold
 * the full RecorderConfig pointer. */
static bool s_ns_enabled  = false;   /**< Noise suppression gate          */
static bool s_vad_enabled = false;   /**< VAD gate                        */

/* Voice level state — updated every chunk, read by the UI via
 * audio_process_get_voice_level().  Volatile is sufficient for a single
 * float on a 32-bit MCU (aligned, atomic reads). */
static volatile float s_voice_level   = 0.0f;
static volatile bool  s_voice_active  = false;

#if HAS_ESP_SR
static esp_afe_sr_iface_t *s_afe_iface = NULL;
static int s_afe_channel_count = 0;
#endif

/* ── Process task ────────────────────────────────────────────────────── */

static void process_task(void *arg)
{
    (void)arg;

    /* Local PCM buffer — stack-allocated.
     * 256 stereo frames → 512 samples × 2 bytes = 1024 bytes.
     * 256 mono samples → 256 samples × 2 bytes = 512 bytes.              */
    int16_t stereo_buf[AUDIO_PROCESS_CHUNK_FRAMES * AUDIO_CAPTURE_CHANNELS];
    int16_t mono_buf[AUDIO_PROCESS_CHUNK_FRAMES];

    ESP_LOGI(TAG, "Process task started (prio %d, stack %d, ns=%d, vad=%d)",
             (int)AUDIO_PROCESS_TASK_PRIORITY,
             (int)AUDIO_PROCESS_TASK_STACK_SIZE,
             (int)s_ns_enabled, (int)s_vad_enabled);

    while (s_running) {
        /* ── Read stereo from the capture ring buffer ───────────────── */
        size_t avail = audio_ringbuf_available(s_input_rb);
        if (avail < AUDIO_PROCESS_CHUNK_FRAMES) {
            /* Not enough data yet — yield and wait for the capture task */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        size_t read = audio_ringbuf_read(s_input_rb, stereo_buf,
                                         AUDIO_PROCESS_CHUNK_FRAMES);
        if (read == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* ── Process: AFE or fallback downmix ───────────────────────── */
        bool voice_detected = false;

#if HAS_ESP_SR
        if (s_afe_iface && s_ns_enabled) {
            /* Feed stereo frames into the AFE.
             * The AFE expects interleaved 16-bit PCM. */
            int feed_ret = s_afe_iface->feed(s_afe_iface,
                                             (int16_t *)stereo_buf);
            if (feed_ret < 0) {
                ESP_LOGW(TAG, "AFE feed returned %d", feed_ret);
            }

            /* Fetch processed output */
            afe_fetch_result_t *res = s_afe_iface->fetch(s_afe_iface);
            if (res && res->data && res->data_size > 0) {
                /* Copy mono PCM out of the fetch result.
                 * data_size is in bytes; convert to sample count. */
                size_t mono_samples = res->data_size / sizeof(int16_t);
                if (mono_samples > AUDIO_PROCESS_CHUNK_FRAMES) {
                    mono_samples = AUDIO_PROCESS_CHUNK_FRAMES;
                }
                memcpy(mono_buf, res->data, mono_samples * sizeof(int16_t));

                /* VAD state from AFE */
                if (s_vad_enabled) {
                    voice_detected = (res->vad_state == AFE_VAD_SPEECH);
                }

                /* Zero-fill remainder if AFE returned fewer samples */
                if (mono_samples < AUDIO_PROCESS_CHUNK_FRAMES) {
                    memset(mono_buf + mono_samples, 0,
                           (AUDIO_PROCESS_CHUNK_FRAMES - mono_samples)
                           * sizeof(int16_t));
                }
            } else {
                /* AFE fetch returned nothing — output silence */
                memset(mono_buf, 0, sizeof(mono_buf));
            }
        } else
#endif /* HAS_ESP_SR */
        {
            /* Fallback: simple 2ch→mono downmix (no AFE) */
            audio_downmix_2ch_to_mono(stereo_buf, mono_buf, read);

            /* Simple energy-threshold VAD when AFE VAD is disabled or
             * ESP-SR is not available.  Threshold empirically tuned for
             * electret mics in a quiet room. */
            if (s_vad_enabled) {
                float level = voice_level_compute_rms(mono_buf, read);
                voice_detected = (level > 0.02f);  /* ~ -34 dBFS threshold */
            }
        }

        /* ── Compute voice level (smoothed) ─────────────────────────── */
        float raw_level = voice_level_compute_rms(mono_buf, read);
        s_voice_level = voice_level_smooth(raw_level, s_voice_level,
                                           AUDIO_PROCESS_LEVEL_ALPHA);
        s_voice_active = voice_detected;

        /* ── Write mono to output ring buffer ───────────────────────── */
        size_t written = audio_mono_ringbuf_write(s_output_rb, mono_buf,
                                                  read);
        if (written < read) {
            ESP_LOGW(TAG, "Output ring buffer overflow: %zu/%zu written",
                     written, read);
        }

        /* Yield even when data is always available — otherwise this task
         * never takes the "not enough data" branch above (the only place
         * with a vTaskDelay) and starves lower-priority tasks, including
         * IDLE0, triggering the task watchdog.                            */
        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "Process task stopped");
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

esp_err_t audio_process_init(audio_ringbuf_t *input_rb,
                             const RecorderConfig *cfg,
                             audio_mono_ringbuf_t **out_rb)
{
    if (!input_rb || !out_rb) {
        return ESP_ERR_INVALID_ARG;
    }

    s_input_rb = input_rb;

    /* Snapshot config booleans so the task doesn't hold a dangling
     * pointer to the config struct. */
    if (cfg) {
        s_ns_enabled  = cfg->noise_suppression;
        s_vad_enabled = cfg->voice_detection;
    } else {
        /* No config provided → safe defaults: NS on, VAD on */
        s_ns_enabled  = true;
        s_vad_enabled = true;
    }

    /* ── Allocate output ring buffer ───────────────────────────────── */
    s_output_rb = audio_mono_ringbuf_alloc(AUDIO_PROCESS_OUTPUT_BUF_SAMPLES);
    if (!s_output_rb) {
        ESP_LOGE(TAG, "Output ring buffer allocation failed (%zu samples)",
                 AUDIO_PROCESS_OUTPUT_BUF_SAMPLES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Output ring buffer: %zu samples (%zu bytes)",
             s_output_rb->capacity_samples,
             s_output_rb->capacity_samples * sizeof(int16_t));

    /* ── Initialise ESP-SR AFE (if available and enabled) ──────────── */
#if HAS_ESP_SR
    if (s_ns_enabled || s_vad_enabled) {
        afe_config_t afe_cfg = {
            .aec_init   = false,   /* No AEC in v1.0                     */
            .se_init    = s_ns_enabled,  /* Noise suppression             */
            .vad_init   = s_vad_enabled, /* Voice activity detection      */
            .agc_init   = true,    /* Moderate AGC per spec §6.2          */
            .wakenet_init = false, /* No WakeNet in v1.0                  */
            .voice_communication_init = false, /* No command recognition  */
            .afe_perferred_core = 1,  /* Run AFE on core 1 (offloads core 0) */
            .afe_perferred_priority = AUDIO_PROCESS_TASK_PRIORITY,
            .afe_ringbuf_size = 50,   /* AFE internal ring buffer frames  */
            .memory_alloc_mode = AFE_MEMORY_ALLOC_PSRAM, /* Use PSRAM     */
            .agc_mode = AFE_MODE_HIGH_PERF,
            .se_mode  = AFE_MODE_HIGH_PERF,
            .vad_mode = AFE_MODE_HIGH_PERF,
            .pcm_config = {
                .total_ch_num = AUDIO_CAPTURE_CHANNELS,  /* 2 mics in     */
                .mic_num      = AUDIO_CAPTURE_CHANNELS,
                .ref_num      = 0,  /* No reference channel for AEC       */
                .sample_rate  = AUDIO_PROCESS_SAMPLE_RATE,
                .afe_mode     = AFE_SR_HIGH_PERF,
            },
        };

        s_afe_iface = esp_afe_handle_from_config(&afe_cfg);
        if (!s_afe_iface) {
            ESP_LOGW(TAG, "ESP-SR AFE init failed — falling back to downmix");
            s_ns_enabled = false;  /* Disable NS so task uses fallback path */
        } else {
            s_afe_channel_count = s_afe_iface->get_channel_num(s_afe_iface);
            ESP_LOGI(TAG, "ESP-SR AFE initialised: ns=%d, vad=%d, agc=1, "
                     "channels_in=%d",
                     (int)s_ns_enabled, (int)s_vad_enabled,
                     s_afe_channel_count);
        }
    } else {
        ESP_LOGI(TAG, "ESP-SR AFE disabled — using downmix fallback");
    }
#else
    ESP_LOGI(TAG, "ESP-SR not available — using downmix fallback");
#endif /* HAS_ESP_SR */

    *out_rb = s_output_rb;
    return ESP_OK;
}

esp_err_t audio_process_start(void)
{
    if (!s_input_rb || !s_output_rb) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_voice_level  = 0.0f;
    s_voice_active = false;

    BaseType_t created = xTaskCreate(
        process_task,
        "audio_proc",
        AUDIO_PROCESS_TASK_STACK_SIZE,
        NULL,
        AUDIO_PROCESS_TASK_PRIORITY,
        &s_task
    );

    if (created != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Process task created");
    return ESP_OK;
}

esp_err_t audio_process_stop(void)
{
    s_running = false;

    /* Give the task a moment to exit its loop */
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_task = NULL;
    }

#if HAS_ESP_SR
    if (s_afe_iface) {
        esp_afe_handle_destroy(s_afe_iface);
        s_afe_iface = NULL;
    }
#endif

    return ESP_OK;
}

float audio_process_get_voice_level(void)
{
    return s_voice_level;
}

bool audio_process_is_voice_active(void)
{
    return s_voice_active;
}
