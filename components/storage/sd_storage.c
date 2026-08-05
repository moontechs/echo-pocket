#include "sd_storage.h"

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "board.h"

static const char *TAG = "sd_storage";

/* ── Opaque handle ───────────────────────────────────────────────────── */
struct sd_storage_s {
    sdmmc_card_t *card;
    bool mounted;
};

/* ── Error strings ───────────────────────────────────────────────────── */
const char *sd_storage_err_str(sd_storage_err_t err)
{
    switch (err) {
    case SD_STORAGE_OK:            return "OK";
    case SD_STORAGE_ERR_MOUNT_FAILED: return "SD mount failed";
    case SD_STORAGE_ERR_DIR_FAILED:   return "SD directory creation failed";
    case SD_STORAGE_ERR_NOT_MOUNTED:  return "SD not mounted";
    default:                          return "unknown";
    }
}

/* ── Directory bootstrap ─────────────────────────────────────────────── */

/** Subdirectories to create under SD_APP_ROOT. Order matters: config
 *  must exist before later tasks open recorder.ini. */
static const char * const APP_SUBDIRS[] = {
    SD_CONFIG_DIR,
    SD_REC_DIR,
    SD_QUEUE_DIR,
    SD_LOGS_DIR,
};
#define APP_SUBDIR_COUNT (sizeof(APP_SUBDIRS) / sizeof(APP_SUBDIRS[0]))

/**
 * Create all echo-pocket subdirectories.
 *
 * @return ESP_OK on success, or the first mkdir error encountered.
 */
static esp_err_t bootstrap_dirs(void)
{
    /* Create the app root first */
    if (mkdir(SD_APP_ROOT, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir(%s) failed: %s", SD_APP_ROOT, strerror(errno));
        return ESP_FAIL;
    }

    for (size_t i = 0; i < APP_SUBDIR_COUNT; i++) {
        if (mkdir(APP_SUBDIRS[i], 0755) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "mkdir(%s) failed: %s", APP_SUBDIRS[i], strerror(errno));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Directory OK: %s", APP_SUBDIRS[i]);
    }

    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────── */

sd_storage_t *sd_storage_init(sd_storage_err_t *out_err)
{
    ESP_LOGI(TAG, "Initializing SD card (SDMMC 1-bit mode)...");

    sd_storage_t *sd = calloc(1, sizeof(*sd));
    if (!sd) {
        if (out_err) *out_err = SD_STORAGE_ERR_MOUNT_FAILED;
        return NULL;
    }

    /* Configure SDMMC host — slot 1 (slot 0 conflicts with octal PSRAM) */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT; /* 4-bit mode per spec (AGENTS.md) */
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; /* 20 MHz */

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = BOARD_SD_PIN_CLK;
    slot.cmd = BOARD_SD_PIN_CMD;
    slot.d0  = BOARD_SD_PIN_D0;
    slot.d1  = BOARD_SD_PIN_D1;
    slot.d2  = BOARD_SD_PIN_D2;
    slot.d3  = BOARD_SD_PIN_D3;
    slot.width = 4;
    /* Disable internal pull-ups — the board has external ones. */
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &sd->card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        free(sd);
        if (out_err) *out_err = SD_STORAGE_ERR_MOUNT_FAILED;
        return NULL;
    }

    ESP_LOGI(TAG, "SD mounted at %s (name=%s, capacity=%llu kB)",
             SD_MOUNT_POINT,
             sd->card->cid.name,
             (unsigned long long)(sd->card->csd.capacity / 1024));

    /* Bootstrap echo-pocket directory tree */
    ret = bootstrap_dirs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Directory bootstrap failed");
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sd->card);
        free(sd);
        if (out_err) *out_err = SD_STORAGE_ERR_DIR_FAILED;
        return NULL;
    }

    sd->mounted = true;
    ESP_LOGI(TAG, "SD storage ready");
    if (out_err) *out_err = SD_STORAGE_OK;
    return sd;
}

void sd_storage_deinit(sd_storage_t *sd)
{
    if (!sd) return;

    if (sd->card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sd->card);
        sd->card = NULL;
    }
    sd->mounted = false;
    free(sd);
    ESP_LOGI(TAG, "SD storage deinitialized");
}

bool sd_storage_is_mounted(const sd_storage_t *sd)
{
    return sd && sd->mounted;
}
