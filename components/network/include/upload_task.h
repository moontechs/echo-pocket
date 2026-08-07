/** @file upload_task.h
 * @brief Upload task — drains the persistent upload queue to Telegram.
 *
 * On Wi-Fi-connected events (Task 14) or manual "Send All" (Task 13),
 * drains QUEUE_STATE_PENDING entries one at a time via the Telegram
 * client (Task 16).
 *
 * Rules (per AGENTS.md and plan Task 17):
 *   - Does nothing if auto_upload=false except in response to explicit
 *     "Send All".
 *   - On send success (ok: true): store telegram_message_id, mark sent.
 *     If delete_after_upload=true, delete the WAV file only after the
 *     queue write confirming "sent" has itself succeeded.
 *   - On failure: increment attempts, revert to pending for retry (no
 *     backoff scheduler in v1.0).  Once attempts reaches a fixed cap
 *     (UPLOAD_MAX_ATTEMPTS = 5), mark failed instead — failed is
 *     terminal and only manual "Send All" resets it.
 *   - Uploads never start mid-recording and never preempt an active
 *     recording.
 *
 * FreeRTOS priority: LOW, below capture/AFE/writer/UI so it never
 * competes with the audio path (see audio_capture.h for the full
 * priority table).
 */

#pragma once

#include "config.h"
#include "queue_store.h"

#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Maximum number of upload attempts before marking failed. */
#define UPLOAD_MAX_ATTEMPTS         5

/** Stack size for the upload task. Lives in PSRAM (see s_upload_task_stack
 *  in upload_task.c), so this isn't fighting the board's scarce internal
 *  RAM budget. telegram_client's sendDocument has ~4.5 KB of stack-local
 *  buffers (multipart pre-body + response) plus deep esp_http_client/
 *  mbedTLS call stack on top — verifying against the full CA bundle
 *  overflowed 8192. */
#define UPLOAD_TASK_STACK_SIZE      20480

/**
 * Upload task priority — LOW, below UI and Wi-Fi.
 * Must stay below capture (HIGH), AFE (HIGH - 1), writer (HIGH - 2),
 * UI (HIGH - 3), and Wi-Fi (HIGH - 4).
 */
#define UPLOAD_TASK_PRIORITY        (configMAX_PRIORITIES - 6)

/* ── Pure-logic drain outcome (unit-testable) ────────────────────────── */

/**
 * Result of a single send attempt, classified for the drain state machine.
 */
typedef enum {
    /** File sent successfully: Telegram responded ok: true. */
    UPLOAD_SEND_OK = 0,

    /** Transient failure: connection drop, HTTP error, API error.
     *  Entry should be retried (up to the cap). */
    UPLOAD_SEND_FAIL_RETRYABLE,

    /** Hard failure: file not found, file too large, null param.
     *  Entry should be marked failed immediately regardless of
     *  remaining attempts. */
    UPLOAD_SEND_FAIL_FATAL,

} upload_send_result_t;

/**
 * Computed outcome of a single drain attempt.
 *
 * This is a pure function — no I/O, no FreeRTOS, no Telegram.
 */
typedef struct {
    queue_state_t new_state;       /**< Resulting queue state             */
    int           new_attempts;    /**< Updated attempt count             */
    bool          should_delete_file; /**< Delete WAV after state is persisted */
} upload_drain_outcome_t;

/**
 * @brief Compute the drain-loop outcome for a single entry given the
 *        send result, current attempt count, and config flags.
 *
 * Pure function — testable without hardware or network.
 *
 * Rules:
 *   - OK → sent, delete file if delete_after_upload is true.
 *   - FAIL_RETRYABLE, attempts+1 < max_attempts → pending (retry), no delete.
 *   - FAIL_RETRYABLE, attempts+1 >= max_attempts → failed, no delete.
 *   - FAIL_FATAL → failed, no delete (regardless of attempts).
 *
 * @param result              Classified send result.
 * @param current_attempts    Entry's current attempt count.
 * @param max_attempts        Cap (UPLOAD_MAX_ATTEMPTS = 5).
 * @param delete_after_upload Config flag from [recorder].delete_after_upload.
 * @return  Computed outcome.
 */
upload_drain_outcome_t upload_drain_compute_outcome(
    upload_send_result_t result,
    int current_attempts,
    int max_attempts,
    bool delete_after_upload);

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialise the upload task.
 *
 * Creates the FreeRTOS task.  The task starts suspended until triggered
 * by a Wi-Fi-connected event or an explicit Send All.
 *
 * @param cfg        Pointer to the loaded config (must outlive the task).
 * @param queue      Initialised upload queue from queue_store_init().
 */
void upload_task_init(const RecorderConfig *cfg, queue_index_t *queue);

/**
 * @brief De-initialise the upload task, stopping and cleaning up.
 */
void upload_task_deinit(void);

/**
 * @brief Trigger a manual "Send All" drain cycle.
 *
 * Called by the UI (Task 13 "Send All" menu item).  Ignores auto_upload
 * — always drains all pending entries regardless of config.
 */
void upload_task_trigger_send_all(void);

#ifdef __cplusplus
}
#endif
