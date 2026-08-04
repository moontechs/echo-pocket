/** @file net_selection.h
 * @brief Pure function: "which Wi-Fi network to try next?"
 *
 * Factored out so it can be unit-tested without any ESP-IDF or mocking
 * framework.  Given a list of configured networks and the result of the
 * last connect attempt, returns the index of the next network to try.
 *
 * Design: pure function with injected inputs — no esp_wifi, no global
 * state.  The Wi-Fi manager calls this before each connect attempt.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types ───────────────────────────────────────────────────────────── */

/** Result of a single Wi-Fi connect attempt. */
typedef enum {
    NET_RESULT_FAILURE = 0,       /**< Connection attempt failed             */
    NET_RESULT_SUCCESS = 1,       /**< Connected successfully                */
} net_connect_result_t;

/** Selection decision returned by net_selection_next. */
typedef enum {
    NET_NEXT_TRY_INDEX,           /**< Use the returned index (0-based)     */
    NET_NEXT_NO_MORE,             /**< Exhausted all networks — stop trying */
    NET_NEXT_ALREADY_CONNECTED,   /**< Still connected — stay on current    */
} net_next_action_t;

/* ── Pure function ───────────────────────────────────────────────────── */

/**
 * @brief Determine which Wi-Fi network to try next.
 *
 * @param num_networks       Number of configured networks (from config).
 * @param last_tried_index   Index of the last network attempted, or -1
 *                           on the very first attempt.
 * @param last_result        Result of the last connect attempt.
 *                           Ignored on first call (last_tried_index == -1).
 * @param is_connected       true if currently connected to an AP.
 * @param out_next_index     [out] If NET_NEXT_TRY_INDEX, the 0-based index
 *                           of the next network to attempt.
 *
 * @return  The action the caller should take.
 */
net_next_action_t net_selection_next(int num_networks,
                                     int last_tried_index,
                                     net_connect_result_t last_result,
                                     bool is_connected,
                                     int *out_next_index);

#ifdef __cplusplus
}
#endif
