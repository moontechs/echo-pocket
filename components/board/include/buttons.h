#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Button identity ─────────────────────────────────────────────────── */

typedef enum {
    BUTTON_LEFT   = 0,
    BUTTON_CENTER = 1,
    BUTTON_RIGHT  = 2,
} ButtonId;

/* ── Event emitted on short-press release ────────────────────────────── */

typedef struct {
    ButtonId button;
} ButtonEvent;

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialize button GPIOs and start the polling/debounce task.
 *
 * @p queue  FreeRTOS queue to push ButtonEvent onto. Must be created by
 *           the caller before calling buttons_init().
 *
 * Uses pins BOARD_BTN_LEFT_PIN / BOARD_BTN_CENTER_PIN / BOARD_BTN_RIGHT_PIN
 * (active-low, internal pull-up enabled).
 */
void buttons_init(QueueHandle_t queue);

/* ── Debounce state machine (pure logic, testable without hardware) ──── */

/** Maximum samples per button tracked for debounce decision. */
#define BUTTON_DEBOUNCE_SAMPLES 6

/** Debounce time in milliseconds. */
#define BUTTON_DEBOUNCE_MS      30

/**
 * @brief Per-button debounce state — embed or allocate per physical button.
 *
 * All fields are private; use button_debounce_init() / button_debounce_feed().
 */
typedef struct {
    bool   history[BUTTON_DEBOUNCE_SAMPLES]; /* ring of raw readings    */
    uint8_t history_idx;                     /* write position in ring  */
    bool   debounced_pressed;                /* last stable state       */
    bool   event_pending;                    /* release detected        */
} ButtonDebounce;

/**
 * @brief Reset debounce state to released with clean history.
 */
void button_debounce_init(ButtonDebounce *db);

/**
 * @brief Feed one raw GPIO reading.
 *
 * @p db          Pointer to debounce state for one button.
 * @p raw_pressed true when GPIO reads active (low = pressed for this board),
 *                false when GPIO reads inactive (high = released).
 * @p now_ms      Monotonic timestamp (not used by the current sample-counting
 *                implementation; reserved for time-based variants).
 *
 * @return true if a short-press release was detected this sample (caller
 *         should emit a ButtonEvent), false otherwise.
 */
bool button_debounce_feed(ButtonDebounce *db, bool raw_pressed, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
