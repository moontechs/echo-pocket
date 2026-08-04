/** @file net_selection.c
 * @brief Pure function: "which Wi-Fi network to try next?"
 *
 * No ESP-IDF dependencies — compiles under logic_tests directly.
 */

#include "net_selection.h"

net_next_action_t net_selection_next(int num_networks,
                                     int last_tried_index,
                                     net_connect_result_t last_result,
                                     bool is_connected,
                                     int *out_next_index)
{
    /* ── Already connected? Stay put ──────────────────────────────── */
    if (is_connected) {
        return NET_NEXT_ALREADY_CONNECTED;
    }

    /* ── No networks configured ───────────────────────────────────── */
    if (num_networks <= 0) {
        return NET_NEXT_NO_MORE;
    }

    /* ── First attempt — try index 0 ──────────────────────────────── */
    if (last_tried_index < 0) {
        if (out_next_index) *out_next_index = 0;
        return NET_NEXT_TRY_INDEX;
    }

    /* ── Last attempt succeeded? Stay on this network ──────────────── */
    if (last_result == NET_RESULT_SUCCESS) {
        /* We should be connected now — caller will re-check is_connected */
        if (out_next_index) *out_next_index = last_tried_index;
        return NET_NEXT_TRY_INDEX;
    }

    /* ── Last attempt failed — try the next network ────────────────── */
    int next = last_tried_index + 1;
    if (next >= num_networks) {
        return NET_NEXT_NO_MORE;
    }

    if (out_next_index) *out_next_index = next;
    return NET_NEXT_TRY_INDEX;
}
