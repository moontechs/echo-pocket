#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mount point ─────────────────────────────────────────────────────── */
/** VFS mount point for the SD card. All echo-pocket paths are beneath this. */
#define SD_MOUNT_POINT          "/sdcard"

/** echo-pocket app root — everything the firmware touches lives here. */
#define SD_APP_ROOT             SD_MOUNT_POINT "/echo-pocket"
#define SD_CONFIG_DIR           SD_APP_ROOT "/config"
#define SD_REC_DIR              SD_APP_ROOT "/rec"
#define SD_QUEUE_DIR            SD_APP_ROOT "/queue"
#define SD_LOGS_DIR             SD_APP_ROOT "/logs"

/* ── Error codes ─────────────────────────────────────────────────────── */
typedef enum {
    SD_STORAGE_OK = 0,
    SD_STORAGE_ERR_MOUNT_FAILED,   /**< SD card not present or init failed */
    SD_STORAGE_ERR_DIR_FAILED,     /**< Directory bootstrap failed     */
    SD_STORAGE_ERR_NOT_MOUNTED,    /**< Operation on unmounted storage  */
} sd_storage_err_t;

/** Human-readable string for each error code. */
const char *sd_storage_err_str(sd_storage_err_t err);

/* ── Opaque handle ───────────────────────────────────────────────────── */
typedef struct sd_storage_s sd_storage_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * Mount the SD card via SDMMC 1-bit mode and bootstrap the echo-pocket
 * directory tree under /sdcard/echo-pocket/.
 *
 * Directories created:
 *   /sdcard/echo-pocket/config/
 *   /sdcard/echo-pocket/rec/
 *   /sdcard/echo-pocket/queue/
 *   /sdcard/echo-pocket/logs/
 *
 * @param[out] out_err  If non-NULL, receives a specific error code on failure.
 * @return  Opaque handle on success, NULL on failure.
 */
sd_storage_t *sd_storage_init(sd_storage_err_t *out_err);

/**
 * Unmount the SD card and release all resources.
 * Safe to call with NULL.
 */
void sd_storage_deinit(sd_storage_t *sd);

/**
 * @return true if the SD card is mounted and ready for I/O.
 */
bool sd_storage_is_mounted(const sd_storage_t *sd);

#ifdef __cplusplus
}
#endif
