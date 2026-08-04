/** @file device_events.h
 * @brief ESP event base and event IDs for device-state transitions.
 *
 * Lives in components/board/include/ (not components/ui) to break a
 * potential ESP-IDF component REQUIRES cycle: publishers are recorder,
 * network/upload, and board/battery, while the sole subscriber is
 * ui_task.  Putting this header in ui would create a ui ↔ recorder
 * circular dependency.  board has no dependency on either, so it is
 * the neutral home.
 *
 * Publishers:
 *   - recorder  (Tasks 7, 15): recording started/stopped/saving/enqueued
 *   - network   (Task 17): upload progress/success/error
 *   - board     (Task 18): low battery warning/critical
 *
 * Subscriber:
 *   - ui_task   (Task 11): translates events to FaceEvent::setEvent() calls
 */

#pragma once

#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event base declaration ──────────────────────────────────────────── */

/** esp_event base name for all device-state transitions. */
#define RECORDER_EVENTS_BASE  "recorder_events"

ESP_EVENT_DECLARE_BASE(RECORDER_EVENTS);

/* ── Event IDs ───────────────────────────────────────────────────────── */

typedef enum {
    /** Recording started successfully.  Payload: NULL. */
    RECORDER_EVENT_STARTED = 0,

    /** Recording stopped and finalization has begun.
     *  Payload: NULL.  The "Saved" UI state fires after SAVED, not here. */
    RECORDER_EVENT_STOPPED,

    /** WAV header patched, fsync'd, file closed.
     *  Payload: NULL.  UI shows "Saved" on this event. */
    RECORDER_EVENT_SAVED,

    /** Recording enqueued for upload (Task 15 queue_store). */
    RECORDER_EVENT_ENQUEUED,

    /** Upload of a single file has started. */
    RECORDER_EVENT_UPLOAD_STARTED,

    /** Upload succeeded (ok: true). */
    RECORDER_EVENT_UPLOAD_SUCCESS,

    /** Upload failed (will be retried or reach failed state). */
    RECORDER_EVENT_UPLOAD_ERROR,

    /** All pending uploads drained (relevant for manual "Send All"). */
    RECORDER_EVENT_UPLOAD_ALL_DONE,

    /** Battery low warning (≤ 20 %). */
    RECORDER_EVENT_BATTERY_WARNING,

    /** Battery critical (≤ 10 %).  Triggers safe-stop in recorder. */
    RECORDER_EVENT_BATTERY_CRITICAL,

    /* ── Sentinel ────────────────────────────────────────────────── */
    RECORDER_EVENT_COUNT
} recorder_event_id_t;

#ifdef __cplusplus
}
#endif
