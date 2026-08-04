#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "echo-pocket starting...");
    board_init();
    ESP_LOGI(TAG, "echo-pocket initialized successfully");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
