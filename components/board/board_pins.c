#include "board.h"
#include "device_events.h"
#include "esp_log.h"

/* Define the esp_event base declared in device_events.h.
 * This lives here because board is the neutral home — no component
 * dependency cycle (publishers are recorder/network/board, subscriber
 * is ui_task). */
ESP_EVENT_DEFINE_BASE(RECORDER_EVENTS);

static const char *TAG = "board";

void board_init(void)
{
    ESP_LOGI(TAG, "Board init (stub) — pins defined, peripherals not yet initialized");
    /* Peripherals will be initialized by their respective components:
     *   Task 2: LCD (display.c)
     *   Task 3: Buttons (buttons.c)
     *   Task 4: SD (sd_storage.c)
     *   Task 6: Audio I2S + ES7210 (audio_capture.c)
     *   Task 18: Battery (battery.c)
     */
}
