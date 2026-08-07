/** @file queue_store.h
 * @brief Persistent upload queue with atomic JSON writes and crash recovery.
 *
 * Stores queue state in /echo-pocket/queue/index.json per the AGENTS.md
 * §Upload queue schema:
 *
 *   states: recording → pending → uploading → sent | failed
 *
 * On init, any entry left in `uploading` state is recovered to `pending`
 * (crash recovery).  Never deletes unsent files.
 *
 * All disk writes use temp-file + rename (atomic), same pattern as
 * config_save().  Pure-logic functions (serialize, deserialize, recover)
 * are exposed for unit-testing without filesystem dependencies.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Maximum sizes ───────────────────────────────────────────────────── */

/** Maximum length of a recording ID string (e.g. "REC_20260804_215700_001"). */
#define QUEUE_ID_MAX         64

/** Maximum path length for a queue entry's file field. */
#define QUEUE_FILE_MAX      256

/** Maximum entries in the upload queue at once. */
#define QUEUE_MAX_ENTRIES    128

/** Path to the upload queue index file (relative to app root). */
#define QUEUE_INDEX_PATH    "/queue/index.json"

/* ── Queue states ────────────────────────────────────────────────────── */

typedef enum {
    QUEUE_STATE_RECORDING = 0,  /**< Recording in progress (not yet finalized) */
    QUEUE_STATE_PENDING,        /**< Ready for upload                          */
    QUEUE_STATE_UPLOADING,      /**< Upload in progress (crash-revertable)     */
    QUEUE_STATE_SENT,           /**< Successfully delivered to Telegram        */
    QUEUE_STATE_FAILED,         /**< Retry cap reached; terminal               */
} queue_state_t;

/** Human-readable state string for JSON serialization. */
const char *queue_state_str(queue_state_t state);

/** Parse a state string back.  Unknown strings map to QUEUE_STATE_PENDING. */
queue_state_t queue_state_from_str(const char *s);

/* ── Queue entry ─────────────────────────────────────────────────────── */

typedef struct {
    char id[QUEUE_ID_MAX];            /**< Recording ID                       */
    char file[QUEUE_FILE_MAX];        /**< Full path to the WAV on SD          */
    queue_state_t state;              /**< Current state                      */
    uint32_t duration_ms;             /**< Recording duration in ms            */
    uint32_t size;                    /**< WAV file size in bytes              */
    int      attempts;                /**< Upload attempt count                */
    int      telegram_message_id;     /**< Telegram message ID (only on sent)   */
} queue_entry_t;

/* ── Opaque queue handle ─────────────────────────────────────────────── */

typedef struct queue_index_s queue_index_t;

/* ── Error codes ─────────────────────────────────────────────────────── */

typedef enum {
    QUEUE_STORE_OK = 0,
    QUEUE_STORE_ERR_NOT_INITIALIZED,   /**< queue_store_init() not called yet   */
    QUEUE_STORE_ERR_FULL,              /**< Queue is at QUEUE_MAX_ENTRIES       */
    QUEUE_STORE_ERR_IO,                /**< File read/write/rename failed       */
    QUEUE_STORE_ERR_PARSE,             /**< JSON parse error (recovered safely) */
    QUEUE_STORE_ERR_NOT_FOUND,         /**< Entry not found in queue            */
} queue_store_err_t;

const char *queue_store_err_str(queue_store_err_t err);

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialise the queue store: load index.json from disk, recover
 *        any `uploading` entries to `pending`.
 *
 * If the file does not exist, starts with an empty queue.
 * If the file exists but is unparseable, logs a warning and starts empty
 * (never crashes on bad queue data).
 *
 * @param base_path  SD app root, e.g. "/sdcard/echo-pocket".
 * @param[out] out_err  If non-NULL, receives the error code.
 * @return  Allocated queue index (never NULL even on partial failure).
 *          Caller must free with queue_store_deinit().
 */
queue_index_t *queue_store_init(const char *base_path,
                                queue_store_err_t *out_err);

/**
 * @brief Flush the queue to disk and free all memory.
 * Safe to call with NULL.
 */
void queue_store_deinit(queue_index_t *queue);

/**
 * @brief Enqueue a new recording as QUEUE_STATE_PENDING.
 *
 * Call right after the WAV is fsync'd+closed (recorder.c finalize path).
 *
 * @return QUEUE_STORE_OK on success.
 */
queue_store_err_t queue_store_enqueue(queue_index_t *queue,
                                      const char *id,
                                      const char *file_path,
                                      uint32_t duration_ms,
                                      uint32_t size);

/**
 * @brief Get the next entry in QUEUE_STATE_PENDING, or NULL.
 *
 * This is the entry the upload task (Task 17) should drain next.
 * The entry pointer is valid until the next queue_store_* call that
 * modifies the underlying array (may realloc).
 *
 * @return Pointer to the first pending entry, or NULL if none.
 */
queue_entry_t *queue_store_get_next_pending(queue_index_t *queue);

/**
 * @brief Transition an entry to QUEUE_STATE_UPLOADING and persist.
 *
 * Called by the upload task just before starting the Telegram send.
 */
queue_store_err_t queue_store_mark_uploading(queue_index_t *queue,
                                             queue_entry_t *entry);

/**
 * @brief Transition an entry to QUEUE_STATE_SENT with a message_id,
 *        and persist.
 *
 * Called by the upload task only after Telegram responds ok: true.
 */
queue_store_err_t queue_store_mark_sent(queue_index_t *queue,
                                        queue_entry_t *entry,
                                        int telegram_message_id);

/**
 * @brief Transition an entry to QUEUE_STATE_FAILED and persist.
 *
 * Called when the retry cap is reached (Task 17).
 */
queue_store_err_t queue_store_mark_failed(queue_index_t *queue,
                                          queue_entry_t *entry);

/**
 * @brief Revert an entry back to PENDING state with an updated attempts count,
 *        and persist.  Used by the upload task after a retryable send failure.
 *
 * Unlike reset_for_send_all (which resets attempts to 0), this preserves
 * and increments the attempts counter so the retry cap is enforced.
 */
queue_store_err_t queue_store_revert_to_pending(queue_index_t *queue,
                                                queue_entry_t *entry,
                                                int new_attempts);

/**
 * @brief Reset a FAILED entry back to PENDING (manual "Send All").
 */
queue_store_err_t queue_store_reset_for_send_all(queue_index_t *queue,
                                                 queue_entry_t *entry);

/**
 * @brief Count entries in PENDING or FAILED state (for UI badge).
 */
int queue_store_count_pending_failed(const queue_index_t *queue);

/**
 * @brief Count entries in PENDING state only.
 */
int queue_store_count_pending(const queue_index_t *queue);

/**
 * @brief Count entries in FAILED state only.
 */
int queue_store_count_failed(const queue_index_t *queue);

/**
 * @brief Count entries in SENT state (for the "Delete Sent" confirm screen).
 */
int queue_store_count_sent(const queue_index_t *queue);

/**
 * @brief Delete the WAV file and queue entry for every SENT recording.
 *
 * Unsent (pending/uploading/failed) entries are never touched, matching
 * the "never deletes unsent files" guarantee of this store.
 *
 * @return  Number of recordings deleted.
 */
int queue_store_delete_sent(queue_index_t *queue);

/**
 * @brief Clear every entry from the queue, regardless of state, and persist.
 *
 * For use only alongside a full recordings wipe (sd_storage_delete_all_recordings()) —
 * dropping entries whose files still exist would silently abandon them.
 *
 * @return  Number of entries removed.
 */
int queue_store_delete_all(queue_index_t *queue);

/**
 * @brief Return the number of entries in the queue.
 */
int queue_store_entry_count(const queue_index_t *queue);

/**
 * @brief Get a pointer to the raw entries array and count.
 * For use by the Unsent list screen (Task 13).
 *
 * @param queue           Queue handle.
 * @param[out] out_count  Receives the number of entries.
 * @return  Pointer to the entries array, or NULL.
 */
const queue_entry_t *queue_store_get_entries(const queue_index_t *queue,
                                             int *out_count);

/* ── Pure-logic functions (unit-testable without filesystem) ─────────── */

/**
 * @brief Serialize an array of entries to the queue JSON format.
 *
 * Pure function — no I/O, no global state.
 *
 * @param entries    Array of entries.
 * @param count      Number of entries.
 * @param buf        Output buffer.
 * @param buf_size   Size of output buffer.
 * @return  Number of bytes written (excluding NUL), or 0 on error.
 */
size_t queue_serialize(const queue_entry_t *entries, int count,
                       char *buf, size_t buf_size);

/**
 * @brief Deserialize queue JSON into a pre-allocated entries array.
 *
 * Pure function — no I/O, no global state.
 *
 * @param content    Null-terminated JSON string.
 * @param entries    Pre-allocated entries array (capacity QUEUE_MAX_ENTRIES).
 * @param capacity   Maximum number of entries the array can hold.
 * @param[out] out_count  Receives the number of parsed entries.
 * @return  Number of entries parsed, or -1 on parse error.
 */
int queue_deserialize(const char *content,
                      queue_entry_t *entries, int capacity,
                      int *out_count);

/**
 * @brief Recover entries from UPLOADING to PENDING state.
 *
 * Pure function — scans the array and fixes any uploading entries.
 *
 * @param entries  Array of entries.
 * @param count    Number of entries.
 * @return  Number of entries recovered.
 */
int queue_recover_uploading(queue_entry_t *entries, int count);

/**
 * @brief Remove every entry in the given state from the array (pure —
 *        no I/O, no global state).  Compacts remaining entries in place.
 *        Caller is responsible for deleting each removed entry's file
 *        beforehand (see queue_store_delete_sent()).
 *
 * @param entries  Array of entries (modified in place).
 * @param count    In/out entry count.
 * @param state    State to remove.
 * @return  Number of entries removed.
 */
int queue_remove_by_state(queue_entry_t *entries, int *count,
                          queue_state_t state);

#ifdef __cplusplus
}
#endif
