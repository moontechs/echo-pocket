#include "sd_storage.h"

const char *sd_storage_err_str(sd_storage_err_t err)
{
    switch (err) {
    case SD_STORAGE_OK: return "OK";
    case SD_STORAGE_ERR_MOUNT_FAILED: return "SD mount failed";
    case SD_STORAGE_ERR_DIR_FAILED: return "SD directory creation failed";
    case SD_STORAGE_ERR_NOT_MOUNTED: return "SD not mounted";
    default: return "unknown";
    }
}
