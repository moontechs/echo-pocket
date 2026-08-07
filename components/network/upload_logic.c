/** @file upload_logic.c
 * @brief Pure upload drain state transition logic.
 */

#include "upload_task.h"

upload_drain_outcome_t upload_drain_compute_outcome(
    upload_send_result_t result, int current_attempts, int max_attempts,
    bool delete_after_upload)
{
    upload_drain_outcome_t out;
    int next_attempts = current_attempts + 1;

    switch (result) {
    case UPLOAD_SEND_OK:
        out.new_state = QUEUE_STATE_SENT;
        out.new_attempts = next_attempts;
        out.should_delete_file = delete_after_upload;
        break;
    case UPLOAD_SEND_FAIL_RETRYABLE:
        out.new_state = (next_attempts >= max_attempts) ? QUEUE_STATE_FAILED : QUEUE_STATE_PENDING;
        out.new_attempts = next_attempts;
        out.should_delete_file = false;
        break;
    case UPLOAD_SEND_FAIL_FATAL:
        out.new_state = QUEUE_STATE_FAILED;
        out.new_attempts = next_attempts;
        out.should_delete_file = false;
        break;
    default:
        out.new_state = QUEUE_STATE_PENDING;
        out.new_attempts = next_attempts;
        out.should_delete_file = false;
        break;
    }
    return out;
}
