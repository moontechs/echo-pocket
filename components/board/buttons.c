#include "buttons.h"
#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "buttons";

/* ── Polling period ──────────────────────────────────────────────────── */

#define POLL_PERIOD_MS 5
#define DEBOUNCE_STABLE_COUNT 6  /* must match BUTTON_DEBOUNCE_SAMPLES */

/* ── Per-button metadata ─────────────────────────────────────────────── */

typedef struct {
    gpio_num_t   pin;
    ButtonId     id;
} ButtonPin;

static const ButtonPin s_buttons[] = {
    { .pin = BOARD_BTN_LEFT_PIN,   .id = BUTTON_LEFT   },
    { .pin = BOARD_BTN_CENTER_PIN, .id = BUTTON_CENTER },
    { .pin = BOARD_BTN_RIGHT_PIN,  .id = BUTTON_RIGHT  },
};
#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

/* ── Task globals ────────────────────────────────────────────────────── */

static QueueHandle_t s_event_queue;
static ButtonDebounce s_debounce[NUM_BUTTONS];

/* ── Debounce state machine (pure logic) ─────────────────────────────── */

void button_debounce_init(ButtonDebounce *db)
{
    db->history_idx = 0;
    db->debounced_pressed = false;
    db->event_pending = false;
    for (int i = 0; i < BUTTON_DEBOUNCE_SAMPLES; i++) {
        db->history[i] = false; /* all samples start as released */
    }
}

bool button_debounce_feed(ButtonDebounce *db, bool raw_pressed, uint32_t now_ms)
{
    (void)now_ms; /* reserved for time-based variant */

    /* Write the new sample into the ring buffer */
    db->history[db->history_idx] = raw_pressed;
    db->history_idx = (db->history_idx + 1) % BUTTON_DEBOUNCE_SAMPLES;

    /* Check if all history entries agree (stable) */
    bool all_same = true;
    for (int i = 0; i < BUTTON_DEBOUNCE_SAMPLES; i++) {
        if (db->history[i] != raw_pressed) {
            all_same = false;
            break;
        }
    }

    if (!all_same) {
        /* Not stable — don't change debounced state, no event */
        db->event_pending = false;
        return false;
    }

    /* Stable — transitions matter */
    if (raw_pressed && !db->debounced_pressed) {
        /* Transition: released → pressed */
        db->debounced_pressed = true;
        db->event_pending = false;  /* no event on press, only on release */
        return false;
    }

    if (!raw_pressed && db->debounced_pressed) {
        /* Transition: pressed → released → emit event */
        db->debounced_pressed = false;
        db->event_pending = false;
        return true; /* short-press release detected */
    }

    /* Stable, no transition */
    db->event_pending = false;
    return false;
}

/* ── Polling task ────────────────────────────────────────────────────── */

static void buttons_task(void *pvParameters)
{
    (void)pvParameters;

    ButtonEvent event;

    /* Initialize all debounce states */
    for (int i = 0; i < NUM_BUTTONS; i++) {
        button_debounce_init(&s_debounce[i]);
    }

    ESP_LOGI(TAG, "Buttons task started (polling every %d ms, debounce %d ms)",
             POLL_PERIOD_MS, BUTTON_DEBOUNCE_MS);

    while (1) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            /* Active-low: 0 = pressed, 1 = released */
            int level = gpio_get_level(s_buttons[i].pin);
            bool raw_pressed = (level == 0);

            if (button_debounce_feed(&s_debounce[i], raw_pressed, 0)) {
                event.button = s_buttons[i].id;
                /* Non-blocking send — if queue is full, drop the event
                 * (better than blocking the button polling task). */
                if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Button event dropped — queue full");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void buttons_init(QueueHandle_t queue)
{
    s_event_queue = queue;

    /* Configure all button GPIOs as inputs with internal pull-ups.
     * Buttons are active-low: pressing pulls the pin to GND. */
    gpio_config_t io_conf = {
        .pin_bit_mask = 0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < NUM_BUTTONS; i++) {
        io_conf.pin_bit_mask |= (1ULL << s_buttons[i].pin);
    }

    gpio_config(&io_conf);

    /* Create the polling task.
     * Stack: on-device capture showed "A stack overflow in task buttons
     * has been detected" at 2048 bytes (during boot, gpio_config()/
     * ESP_LOG call chains apparently exceed it) — bumped to 3072.
     * Priority: below audio capture but above UI/network (per Task 6 priority
     *           order — capture > AFE/writer > UI > network/upload).
     *           Actual priority: 5 (mid-range, below audio at 8-10). */
    BaseType_t ret = xTaskCreate(
        buttons_task,
        "buttons",
        3072,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create buttons task");
    } else {
        ESP_LOGI(TAG, "Buttons initialized (left=%d, center=%d, right=%d)",
                 BOARD_BTN_LEFT_PIN, BOARD_BTN_CENTER_PIN, BOARD_BTN_RIGHT_PIN);
    }
}
