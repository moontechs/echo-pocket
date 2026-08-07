#include "board.h"
#include "device_events.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Define the esp_event base declared in device_events.h.
 * This lives here because board is the neutral home — no component
 * dependency cycle (publishers are recorder/network/board, subscriber
 * is ui_task). */
ESP_EVENT_DEFINE_BASE(RECORDER_EVENTS);

static const char *TAG = "board";

void board_init(void)
{
    /* Latch the battery power path on (high = on, per BOARD_BAT_POWER_PIN
     * doc). Must happen first: on battery-only power the board briefly
     * comes up through a momentary/self-latching path, and stays powered
     * only once this pin is actively driven high. Without it the board
     * only boots when USB supplies power directly. */
    gpio_config_t bat_power_cfg = {
        .pin_bit_mask = (1ULL << BOARD_BAT_POWER_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&bat_power_cfg);
    gpio_set_level(BOARD_BAT_POWER_PIN, 1);

    ESP_LOGI(TAG, "Board init — battery power latched on, other peripherals init separately");
    /* Peripherals will be initialized by their respective components:
     *   Task 2: LCD (display.c)
     *   Task 3: Buttons (buttons.c)
     *   Task 4: SD (sd_storage.c)
     *   Task 6: Audio I2S + ES7210 (audio_capture.c)
     *   Task 18: Battery (battery.c)
     */
}

void board_power_off(void)
{
    ESP_LOGI(TAG, "Powering off — dropping BAT_POWER_PIN");
    gpio_set_level(BOARD_BAT_POWER_PIN, 0);
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
