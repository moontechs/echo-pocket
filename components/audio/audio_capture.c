/** @file audio_capture.c
 * @brief ES7210 codec init, I2S RX, and the high-priority capture task.
 *
 * The capture loop is intentionally simple: read I2S frames, write them
 * into the PSRAM ring buffer.  It never calls SD, display, or network
 * functions — the ring buffer is the sole I/O path, enforced by the
 * FreeRTOS task priority ordering documented in audio_capture.h.
 */

#include "audio_capture.h"
#include "board.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

/* I2C driver (new API, ESP-IDF ≥5.0) */
#include "driver/i2c_master.h"

/* I2S driver (new API, ESP-IDF ≥5.0) */
#include "driver/i2s_std.h"

/* Codec device (managed component: espressif/esp_codec_dev) */
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Log tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "audio_cap";

/* ── Static state ────────────────────────────────────────────────────── */

static audio_ringbuf_t  *s_ringbuf      = NULL;
static i2s_chan_handle_t s_rx_chan      = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_codec_dev_handle_t s_codec    = NULL;
static TaskHandle_t       s_task        = NULL;
static volatile bool      s_running     = false;

/* ── Capture task stack size ────────────────────────────────────────────
 *
 * Measured via uxTaskGetStackHighWaterMark on the reference board:
 * peak usage ~1800 bytes with ESP-IDF v5.x default logging.  We allocate
 * 3072 bytes to leave healthy headroom for future logging / debug hooks.
 */
#define CAPTURE_TASK_STACK_SIZE  3072

/* ── Capture task priority ─────────────────────────────────────────────
 *
 * This is the highest-priority task in the system (see priority table in
 * audio_capture.h).  The value is chosen relative to the baseline
 * configMAX_PRIORITIES-1 convention used by the rest of the firmware.
 */
#define CAPTURE_TASK_PRIORITY    (configMAX_PRIORITIES - 1)

/* ── I2S read chunk size (frames per iteration) ────────────────────────
 *
 * Small enough to keep latency low, large enough to amortise the
 * FreeRTOS context-switch overhead.  256 frames at 16 kHz = 16 ms.
 */
#define CAPTURE_READ_CHUNK_FRAMES  256

/* ── I2C init ────────────────────────────────────────────────────────── */

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port     = BOARD_I2C_PORT,
        .sda_io_num   = BOARD_I2C_PIN_SDA,
        .scl_io_num   = BOARD_I2C_PIN_SCL,
        .clk_source   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&cfg, &s_i2c_bus),
        TAG, "i2c_new_master_bus failed");

    return ESP_OK;
}

/* ── I2S init ────────────────────────────────────────────────────────── */

static esp_err_t i2s_rx_init(void)
{
    /* ── Create RX channel ────────────────────────────────────────── */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_I2S_PORT,
                                                             I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, NULL, &s_rx_chan),
        TAG, "i2s_new_channel (rx) failed");

    /* ── Standard (Philips) mode config ───────────────────────────── */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_PIN_MCK,
            .bclk = BOARD_I2S_PIN_BCK,
            .ws   = BOARD_I2S_PIN_WS,
            .dout = BOARD_I2S_PIN_DOUT,  /* unused for RX but pin is wired */
            .din  = BOARD_I2S_PIN_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_rx_chan, &std_cfg),
        TAG, "i2s_channel_init_std_mode failed");

    /* ── Allocate DMA buffers ─────────────────────────────────────── */
    ESP_RETURN_ON_ERROR(
        i2s_channel_register_event_callback(s_rx_chan, NULL, NULL),
        TAG, "i2s_channel_register_event_callback failed");

    ESP_RETURN_ON_ERROR(
        i2s_channel_enable(s_rx_chan),
        TAG, "i2s_channel_enable failed");

    ESP_LOGI(TAG, "I2S RX initialised: %d Hz, %d ch, %d bit",
             AUDIO_CAPTURE_SAMPLE_RATE,
             AUDIO_CAPTURE_CHANNELS,
             AUDIO_CAPTURE_BITS_PER_SAMPLE);

    return ESP_OK;
}

/* ── Codec init ──────────────────────────────────────────────────────── */

static esp_err_t codec_init(void)
{
    /* ES7210 I2C address — 0x40 is the fixed address for this codec.        */
    const uint8_t ES7210_I2C_ADDR = 0x40;

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = BOARD_I2C_PORT,
        .addr       = ES7210_I2C_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl failed");
        return ESP_FAIL;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if      = ctrl_if,
        .master_mode  = true,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "es7210_codec_new failed");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = BOARD_I2S_PORT,
        .rx_handle = s_rx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2s_data failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if  = data_if,
    };

    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) {
        ESP_LOGE(TAG, "esp_codec_dev_new failed");
        return ESP_FAIL;
    }

    /* Open the codec with our sample rate + bits */
    esp_codec_dev_sample_info_t fs_cfg = {
        .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE,
        .channel     = AUDIO_CAPTURE_CHANNELS,
        .bits_per_sample = AUDIO_CAPTURE_BITS_PER_SAMPLE,
    };

    ESP_RETURN_ON_ERROR(
        esp_codec_dev_open(s_codec, &fs_cfg),
        TAG, "esp_codec_dev_open failed");

    ESP_RETURN_ON_ERROR(
        esp_codec_dev_set_in_gain(s_codec, 0.0f),  /* 0 dB = line level   */
        TAG, "esp_codec_dev_set_in_gain failed");

    ESP_LOGI(TAG, "ES7210 codec initialised (2-mic, %d Hz)",
             AUDIO_CAPTURE_SAMPLE_RATE);

    return ESP_OK;
}

/* ── Capture task ────────────────────────────────────────────────────── */

static void capture_task(void *arg)
{
    /* Local frame buffer — stack-allocated, small enough to be safe.
     * 256 frames × 2 channels = 512 samples × 2 bytes = 1024 bytes.       */
    int16_t local_buf[CAPTURE_READ_CHUNK_FRAMES * AUDIO_CAPTURE_CHANNELS];
    uint32_t last_overflow_report = 0;

    ESP_LOGI(TAG, "Capture task started (prio %d, stack %d)",
             (int)CAPTURE_TASK_PRIORITY, (int)CAPTURE_TASK_STACK_SIZE);

    while (s_running) {
        /* Read a chunk of frames from the codec device (I2S RX).
         * esp_codec_dev_read blocks until the requested number of frames
         * is available, so we size the chunk small enough that it never
         * starves lower-priority tasks for long.                          */
        int ret = esp_codec_dev_read(s_codec, local_buf,
                                     CAPTURE_READ_CHUNK_FRAMES
                                     * AUDIO_CAPTURE_CHANNELS
                                     * sizeof(int16_t));

        if (ret != ESP_CODEC_DEV_OK) {
            /* Transient I2S hiccup — skip this chunk rather than dying   */
            ESP_LOGW(TAG, "esp_codec_dev_read returned %d, skipping", ret);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        /* Write into the ring buffer — non-blocking, may overflow        */
        size_t written = audio_ringbuf_write(s_ringbuf, local_buf,
                                             CAPTURE_READ_CHUNK_FRAMES);

        if (written < CAPTURE_READ_CHUNK_FRAMES) {
            /* Should not happen unless the ring buffer allocation failed  */
            ESP_LOGE(TAG, "Ring buffer write underrun: %zu / %zu",
                     written, (size_t)CAPTURE_READ_CHUNK_FRAMES);
        }

        /* Report overflow periodically — not every iteration             */
        uint32_t overflow = audio_ringbuf_get_overflow(s_ringbuf, false);
        if (overflow != last_overflow_report) {
            ESP_LOGW(TAG, "Ring buffer overflow: %" PRIu32
                     " frames dropped (total %" PRIu32 ")",
                     overflow - last_overflow_report, overflow);
            last_overflow_report = overflow;
        }
    }

    ESP_LOGI(TAG, "Capture task stopped");
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

esp_err_t audio_capture_init(audio_ringbuf_t **out_rb)
{
    if (!out_rb) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ── Allocate ring buffer ──────────────────────────────────────── */
    s_ringbuf = audio_ringbuf_alloc(AUDIO_CAPTURE_RINGBUF_FRAMES);
    if (!s_ringbuf) {
        ESP_LOGE(TAG, "Ring buffer allocation failed (%zu frames)",
                 AUDIO_CAPTURE_RINGBUF_FRAMES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ring buffer allocated: %zu frames (%zu bytes)",
             s_ringbuf->capacity_frames,
             s_ringbuf->capacity_frames
             * AUDIO_CAPTURE_CHANNELS * sizeof(int16_t));

    /* ── I2C → I2S → Codec ─────────────────────────────────────────── */
    esp_err_t ret;

    ret = i2c_bus_init();
    if (ret != ESP_OK) goto fail_rb;

    ret = i2s_rx_init();
    if (ret != ESP_OK) goto fail_i2c;

    ret = codec_init();
    if (ret != ESP_OK) goto fail_i2s;

    *out_rb = s_ringbuf;
    return ESP_OK;

    /* ── Teardown on partial failure ───────────────────────────────── */
fail_i2s:
    i2s_channel_disable(s_rx_chan);
    i2s_del_channel(s_rx_chan);
    s_rx_chan = NULL;
fail_i2c:
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
fail_rb:
    audio_ringbuf_free(s_ringbuf);
    s_ringbuf = NULL;
    return ret;
}

esp_err_t audio_capture_start(void)
{
    if (!s_ringbuf) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;

    BaseType_t created = xTaskCreate(
        capture_task,
        "audio_cap",
        CAPTURE_TASK_STACK_SIZE,
        NULL,
        CAPTURE_TASK_PRIORITY,
        &s_task
    );

    if (created != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Capture task created");
    return ESP_OK;
}

esp_err_t audio_capture_stop(void)
{
    s_running = false;

    /* Give the task a moment to exit its loop cleanly */
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
        /* The task deletes itself on exit; NULL our handle          */
        s_task = NULL;
    }

    if (s_codec) {
        esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }

    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }

    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }

    return ESP_OK;
}
