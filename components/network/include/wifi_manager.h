/** @file wifi_manager.h
 * @brief Wi-Fi connection manager + SNTP time sync for echo-pocket.
 *
 * Reads [wifi_N] entries from the loaded RecorderConfig (Task 5),
 * tries them in order (delegates ordering to the pure net_selection_next
 * function), connects, and auto-reconnects on drop.
 *
 * On first successful connect:
 *   1. Runs SNTP to set the system clock.
 *   2. Applies [device].timezone via setenv("TZ",…) / tzset().
 *   3. Sets the "time synced" flag in recorder (recorder_set_time_synced).
 *   4. Posts RECORDER_EVENT_WIFI_CONNECTED.
 *
 * When disconnected, posts RECORDER_EVENT_WIFI_DISCONNECTED.
 * Runs fully degraded (no crash, no blocking) when no network is reachable.
 *
 * FreeRTOS task: wifi_task runs at below-recorder priority — see
 * audio_capture.h for the full task priority table.
 */

#pragma once

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Stack size for the Wi-Fi manager task. */
#define WIFI_MANAGER_TASK_STACK_SIZE    4096

/**
 * Wi-Fi manager task priority — LOW, below UI and upload.
 * Must stay below capture (HIGH), AFE (HIGH - 1), writer (HIGH - 2),
 * and UI (HIGH - 3).
 */
#define WIFI_MANAGER_TASK_PRIORITY      (configMAX_PRIORITIES - 5)

/** Bit in the Wi-Fi event group: connected. */
#define WIFI_CONNECTED_BIT  (1 << 0)

/** Bit in the Wi-Fi event group: disconnected. */
#define WIFI_DISCONNECTED_BIT (1 << 1)

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the Wi-Fi manager task.
 *
 * Does NOT block — the task runs asynchronously.  If no Wi-Fi networks
 * are configured, the task starts but remains idle (no crash).
 *
 * @param cfg  Pointer to the loaded config (must remain valid for the
 *             lifetime of the task — typically statically allocated).
 */
void wifi_manager_init(const RecorderConfig *cfg);

/**
 * @brief De-initialise the Wi-Fi manager, disconnecting and stopping
 *        the task.
 */
void wifi_manager_deinit(void);

/**
 * @brief Check whether Wi-Fi is currently connected.
 *
 * @return true if connected to an AP.
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
