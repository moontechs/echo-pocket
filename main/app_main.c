#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "display.h"
#include "sd_storage.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "echo-pocket starting...");
    board_init();
    display_init();

    /* Mount SD card and bootstrap directory tree */
    sd_storage_err_t sd_err = SD_STORAGE_OK;
    sd_storage_t *sd = sd_storage_init(&sd_err);
    if (!sd) {
        ESP_LOGE(TAG, "SD init failed: %s", sd_storage_err_str(sd_err));
        display_draw_text(32, 200, "SD ERROR - check card",
                          (uint16_t)0xF800); /* red */
        /* Do not crash — keep the UI alive so the error is visible */
    } else {
        ESP_LOGI(TAG, "SD storage mounted");
        display_draw_text(48, 200, "SD OK",
                          (uint16_t)0x07E0); /* green */
    }

    ESP_LOGI(TAG, "echo-pocket initialized");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Not reached in normal operation, but clean for completeness */
    sd_storage_deinit(sd);
}
