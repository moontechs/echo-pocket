/** @file app_main.c
 * @brief Boot sequence — wires all components in order per Task 19.
 *
 * Boot order: board → display → SD mount → config → queue (recover
 * uploading→pending) → audio capture → audio process (AFE) → recorder →
 * battery → buttons → UI → Wi-Fi → upload.
 *
 * Every stage degrades gracefully — no single failure crashes the firmware.
 * SD recording keeps working even without Wi-Fi/Telegram/config.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "board.h"
#include "device_events.h"
#include "display.h"
#include "sd_storage.h"
#include "config.h"
#include "queue_store.h"
#include "audio_capture.h"
#include "audio_process.h"
#include "recorder.h"
#include "buttons.h"
#include "ui_task.h"
#include "face_registry.h"
#include "wifi_manager.h"
#include "upload_task.h"
#include "battery.h"
#include "telegram_client.h"

static const char *TAG = "app_main";

/* ── Persistent state — config and queue handle must outlive all tasks ─ */

/** Loaded configuration (always valid — defaults filled if file absent). */
static RecorderConfig s_config;

/** SD card handle (NULL if absent / mount failed). */
static sd_storage_t *s_sd = NULL;

/** Upload queue handle (always valid — empty queue if SD absent). */
static queue_index_t *s_queue = NULL;

/** Button event queue — created here, fed by buttons_init, consumed by ui_task. */
static QueueHandle_t s_button_queue = NULL;

/* ── Boot checklist — each check gets its own line, failures get a
 * one-line "what to do" hint underneath. Left on screen so a failure is
 * still readable after boot finishes, not just flashed and overwritten. */

#define BOOT_CHECK_COLOR_OK    0x07E0 /* green */
#define BOOT_CHECK_COLOR_FAIL  0xF800 /* red */
#define BOOT_CHECK_COLOR_HINT  0xFFE0 /* yellow */

static int  s_boot_y = 0;
static bool s_boot_had_failure = false;

static void boot_check(const char *label, bool ok, const char *fail_hint)
{
    display_draw_text(4, s_boot_y, ok ? "[OK]" : "[FAIL]",
                       ok ? BOOT_CHECK_COLOR_OK : BOOT_CHECK_COLOR_FAIL);
    display_draw_text(56, s_boot_y, label, 0xFFFF);
    s_boot_y += 16;

    if (!ok) {
        s_boot_had_failure = true;
        if (fail_hint) {
            display_draw_text(12, s_boot_y, fail_hint, BOOT_CHECK_COLOR_HINT);
            s_boot_y += 16;
        }
    }
}

/* ── Boot sequence ──────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "echo-pocket v1.0 starting...");

    /* ── Step -1: NVS flash — required before esp_wifi_init(). Without
     * this, wifi_manager's task aborts on ESP_ERR_NVS_NOT_INITIALIZED
     * and reboots the device. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* ── Step 0: create the default esp_event loop ────────────────────
     * Must exist before any component registers handlers or posts events.
     * RECORDER_EVENTS base is defined in board/board_pins.c (neutral home). */
    esp_err_t evt_err = esp_event_loop_create_default();
    if (evt_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                 esp_err_to_name(evt_err));
        /* Unrecoverable — event loop is fundamental.  Spin with a log. */
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* ── Step 1: board_init — GPIO, I2C, SPI, event base definition ── */
    board_init();
    ESP_LOGI(TAG, "board initialized");

    /* ── Step 2: display — must be up early so we can show errors ──── */
    display_init();
    display_clear(0x0000);
    display_draw_text(4, s_boot_y, "echo-pocket v1.0 boot checks", 0xFFFF);
    s_boot_y += 20;
    ESP_LOGI(TAG, "display initialized");

    /* ── Step 3: mount SD, bootstrap directories ───────────────────── */
    sd_storage_err_t sd_err = SD_STORAGE_OK;
    s_sd = sd_storage_init(&sd_err);
    if (!s_sd) {
        ESP_LOGW(TAG, "SD init failed: %s — continuing without storage",
                 sd_storage_err_str(sd_err));
        boot_check("SD card", false, "No card, or not FAT32 - check/format");
    } else {
        ESP_LOGI(TAG, "SD storage mounted at %s", SD_MOUNT_POINT);
        boot_check("SD card", true, NULL);
    }

    /* ── Step 4: load config (safe defaults if file missing/malformed) ── */
    config_set_defaults(&s_config);
    config_err_t cfg_err = CONFIG_OK;
    if (s_sd) {
        s_config = config_load("/sdcard/echo-pocket/config/recorder.ini", &cfg_err);
    } else {
        /* No SD — keep defaults, mark as file-not-found */
        cfg_err = CONFIG_ERR_FILE_NOT_FOUND;
    }

    if (cfg_err == CONFIG_OK) {
        ESP_LOGI(TAG, "Config loaded: device='%s', wifi_count=%d, bot_token=%s",
                 s_config.device_name, s_config.wifi_count,
                 s_config.bot_token[0] ? "[set]" : "[not set]");
        boot_check("Config", true, NULL);
    } else {
        ESP_LOGW(TAG, "Config load issue (%s) — using defaults",
                 config_err_str(cfg_err));
        boot_check("Config", false, "Using defaults - check recorder.ini");
    }

    /* ── Step 5: load upload queue + recover uploading→pending ────────
     * queue_store_init handles crash recovery internally: any entry left
     * in 'uploading' state is rewritten to 'pending'.  If SD is absent
     * the queue is empty (in-memory only, lost on reboot). */
    const char *queue_base = s_sd ? "/sdcard/echo-pocket" : NULL;
    queue_store_err_t qs_err = QUEUE_STORE_OK;
    s_queue = queue_store_init(queue_base, &qs_err);
    if (qs_err != QUEUE_STORE_OK) {
        ESP_LOGW(TAG, "Queue init issue (%s) — starting with empty queue",
                 queue_store_err_str(qs_err));
    } else {
        int pending = queue_store_count_pending(s_queue);
        int failed  = queue_store_count_failed(s_queue);
        int total   = queue_store_entry_count(s_queue);
        ESP_LOGI(TAG, "Queue loaded: %d entries (%d pending, %d failed)",
                 total, pending, failed);
    }

    /* ── Step 6: audio capture (I2S + ES7210 + PSRAM ring buffer) ─────
     * Capture starts immediately — WAV writer picks up when told. */
    audio_ringbuf_t *capture_rb = NULL;
    esp_err_t ac_err = audio_capture_init(&capture_rb);
    if (ac_err != ESP_OK) {
        ESP_LOGE(TAG, "audio_capture_init failed: %s — audio unavailable",
                 esp_err_to_name(ac_err));
        boot_check("Audio codec", false, "Check ES7210 mic wiring/power");
    } else {
        ESP_LOGI(TAG, "Audio capture initialized (ringbuf %p)", (void *)capture_rb);
        ac_err = audio_capture_start();
        if (ac_err != ESP_OK) {
            ESP_LOGE(TAG, "audio_capture_start failed: %s",
                     esp_err_to_name(ac_err));
            boot_check("Audio codec", false, "Capture task failed to start");
        } else {
            ESP_LOGI(TAG, "Audio capture task running");
            boot_check("Audio codec", true, NULL);
        }
    }

    /* ── Step 7: audio process (ESP-SR AFE — NS + VAD + AGC) ──────────
     * Gate NS/VAD individually on config flags.  If AFE init fails or
     * is disabled, fall through — the caller must check whether the
     * mono ring buffer was created. */
    audio_mono_ringbuf_t *mono_rb = NULL;
    if (capture_rb) {
        esp_err_t ap_err = audio_process_init(capture_rb, &s_config, &mono_rb);
        if (ap_err != ESP_OK) {
            ESP_LOGW(TAG, "audio_process_init failed: %s — no AFE output",
                     esp_err_to_name(ap_err));
        } else {
            ap_err = audio_process_start();
            if (ap_err != ESP_OK) {
                ESP_LOGW(TAG, "audio_process_start failed: %s",
                         esp_err_to_name(ap_err));
            } else {
                const char *ns_str = s_config.noise_suppression ? " (NS on)" : " (NS off)";
                const char *vad_str = s_config.voice_detection ? " (VAD on)" : " (VAD off)";
                ESP_LOGI(TAG, "Audio process task running%s%s", ns_str, vad_str);
            }
        }
    }

    /* ── Step 8: recorder (sd_writer_task + WAV finalize) ─────────────
     * If the mono ring buffer is NULL (capture or AFE failed), the
     * recorder is unavailable — sd_writer_task never starts. */
    recorder_init(mono_rb);
    if (mono_rb) {
        ESP_LOGI(TAG, "Recorder initialized");
        /* Wire the upload queue so finalized recordings are enqueued. */
        if (s_queue) {
            recorder_set_queue_store(s_queue);
            ESP_LOGI(TAG, "Recorder: queue store wired");
        }
    } else {
        ESP_LOGE(TAG, "Recorder unavailable — no audio ring buffer");
    }

    /* ── Step 9: face registry — register built-in themes ─────────────
     * Must happen before ui_task_init so the active theme is resolved. */
    face_registry_register_defaults();

    /* Apply the theme from config (unknown id falls back to "minimal"). */
    face_registry_begin(s_config.theme);
    ESP_LOGI(TAG, "Face theme: '%s' (active: '%s')",
             s_config.theme, face_registry_get_active_id());

    /* ── Step 10: battery monitoring ──────────────────────────────────
     * Gate on hardware verdict from board.h.  If VBAT is not readable
     * this is a no-op and all queries return UNKNOWN. */
    battery_init();
    if (battery_is_present()) {
        ESP_LOGI(TAG, "Battery monitoring active");
    } else {
        ESP_LOGI(TAG, "Battery monitoring unavailable — showing 'unknown'");
    }

    /* ── Step 11: Wi-Fi manager (non-blocking task) ───────────────────
     * Starts its own FreeRTOS task, connects per [wifi_N], runs SNTP,
     * applies [device].timezone, sets recorder_set_time_synced(). */
    wifi_manager_init(&s_config);
    ESP_LOGI(TAG, "Wi-Fi manager started (networks: %d)", s_config.wifi_count);

    /* ── Step 12: Telegram client initialization ─────────────────────
     * Must happen before upload_task_init() so the client is ready when
     * the first upload is triggered. Skip if bot_token is empty. */
    if (s_config.bot_token[0] != '\0') {
        telegram_err_t tg_err = telegram_client_init(s_config.bot_token);
        if (tg_err != TELEGRAM_OK) {
            ESP_LOGE(TAG, "telegram_client_init failed: %s — uploads disabled",
                     telegram_err_str(tg_err));
            boot_check("Telegram", false, "Check bot_token in recorder.ini");
        } else {
            ESP_LOGI(TAG, "Telegram client initialized");
            boot_check("Telegram", true, NULL);
        }
    } else {
        ESP_LOGI(TAG, "No bot_token configured — Telegram uploads disabled");
    }

    /* ── Step 13: upload task (drains queue on Wi-Fi connect) ─────────
     * Ignores auto_upload=false except for explicit "Send All".
     * upload_task guards against starting mid-recording. */
    upload_task_init(&s_config, s_queue);
    ESP_LOGI(TAG, "Upload task started (auto_upload=%s)",
             s_config.auto_upload ? "true" : "false");

    /* ── Boot checks complete — leave the checklist up so a failure is
     * actually readable before the UI task starts overwriting the screen
     * with the normal home screen. */
    display_draw_text(4, s_boot_y, s_boot_had_failure ? "Boot: issues above" : "Boot: all checks OK",
                       s_boot_had_failure ? BOOT_CHECK_COLOR_FAIL : BOOT_CHECK_COLOR_OK);
    ESP_LOGI(TAG, "echo-pocket v1.0 boot complete%s",
             s_boot_had_failure ? " — with issues, see above" : " — all systems nominal");
    if (s_boot_had_failure) {
        vTaskDelay(pdMS_TO_TICKS(4000));
    }

    /* ── Step 14: buttons + UI task ────────────────────────────────────
     * ui_task is the sole ButtonEvent consumer and sole RECORDER_EVENTS
     * subscriber (maps device-state to FaceEvent).  It calls
     * recorder_start() / recorder_stop() directly.  Started last so it
     * doesn't overwrite the boot checklist above before it can be read. */
    s_button_queue = xQueueCreate(4, sizeof(ButtonEvent));
    if (s_button_queue) {
        buttons_init(s_button_queue);
        ui_task_init(s_button_queue);
        ESP_LOGI(TAG, "Buttons + UI initialized");
    } else {
        ESP_LOGE(TAG, "Failed to create button queue — UI unavailable");
    }

    /* ── Idle loop — all work happens in FreeRTOS tasks ────────────────
     * The main task sleeps forever while capture/AFE/writer/UI/Wi-Fi/upload
     * tasks do their work.  No cleanup path in v1.0 (battery-powered
     * device is power-cycled, not cleanly shut down). */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
