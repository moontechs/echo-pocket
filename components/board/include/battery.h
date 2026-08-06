/** @file battery.h
 * @brief Battery monitoring — ADC read, discharge-curve lookup,
 *        threshold classification, and RECORDER_EVENTS publishing.
 *
 * Hardware verdict (Task 1, from vendor example bsp_power_manager.c):
 *   - VBAT is exposed via a resistor divider to GPIO 1 (ADC1_CH0).
 *   - ADC is calibrated via eFuse curve-fitting (adc_cali).
 *   - Charger status is readable on GPIO 3 (low = charging).
 *   - Battery power can be cut via GPIO 2 (set low = power off).
 *   - No pin conflicts with LCD, SD, I2S, or I2C.
 *
 * Update rate: ~15 s (via FreeRTOS timer).  This is slow enough to
 * avoid Wi-Fi TX voltage-sag noise but fast enough to catch a
 * draining battery during a long recording.
 *
 * Thresholds (single-cell Li-ion 3.7V nominal):
 *   >20 %  → normal
 *   ≤20 %  → warning (publishes RECORDER_EVENT_BATTERY_WARNING)
 *   ≤10 %  → critical (publishes RECORDER_EVENT_BATTERY_CRITICAL,
 *                        triggers safe-stop in recorder,
 *                        blocks large auto-uploads in upload_task)
 *
 * Pure functions (testable under logic_tests, no ADC/FreeRTOS needed):
 *   battery_voltage_to_percent(int voltage_mv) → 0–100
 *   battery_percent_to_threshold(int percent) → enum
 *   battery_threshold_for_upload(int percent, uint32_t file_bytes) → bool
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────── */

/** Battery percent sentinel: monitoring is unavailable. */
#define BATTERY_PERCENT_UNKNOWN      (-1)

/**
 * Auto-upload size ceiling when the battery is ≤ 10 %.
 *
 * ~5 MB ≈ 2–3 minutes of mono 16 kHz s16 audio.  Small enough that a
 * critical-battery upload doesn't drain the last 10 % into a brown-out
 * during the send.  Manual "Send All" ignores this limit.
 *
 * Lives here (not in upload_task.h) so the pure-logic test doesn't
 * need the network component.
 */
#define LOW_BATTERY_UPLOAD_MAX_BYTES (5 * 1024 * 1024)

/** ADC attenuation for the VBAT divider (11 dB for ~0–3.1 V range). */
#define BATTERY_ADC_ATTEN_DB         ADC_ATTEN_DB_11

/** Number of ADC samples averaged per reading. */
#define BATTERY_ADC_SAMPLES          16

/** Battery voltage divider ratio: (Rtop + Rbottom) / Rbottom, 200K/100K. */
#define BATTERY_DIVIDER_RATIO        3.0f

/** Battery update period (ms). */
#define BATTERY_UPDATE_PERIOD_MS     15000

/* ── Threshold classification ────────────────────────────────────────── */

typedef enum {
    BATTERY_NORMAL = 0,   /**< >20 % — full operation allowed              */
    BATTERY_WARNING,      /**< ≤20 % — show warning, allow small uploads   */
    BATTERY_CRITICAL,     /**< ≤10 % — safe-stop recording, block large auto-uploads */
    BATTERY_UNKNOWN,      /**< Monitoring unavailable (no ADC pin)          */
} battery_level_t;

/* ── Pure-logic functions (unit-testable under logic_tests) ──────────── */

/**
 * @brief Convert a raw voltage (mV) to battery percentage.
 *
 * Uses a single-cell Li-ion non-linear discharge curve.
 * Typical range: 3300–4200 mV → 0–100 %.
 *
 * Pure function — no ADC, no state.
 *
 * @param voltage_mv  Battery voltage in millivolts (after divider correction).
 * @return  0–100 (clamped).
 */
int battery_voltage_to_percent(int voltage_mv);

/**
 * @brief Map a battery percentage to a threshold enum.
 *
 * Pure function — testable without ADC.
 *
 * @param percent  0–100, or BATTERY_PERCENT_UNKNOWN for unavailable.
 * @return  Threshold classification.
 */
battery_level_t battery_percent_to_threshold(int percent);

/**
 * @brief Decide whether the battery level should block an auto-upload
 *        of a file this size.
 *
 * Pure function — testable without ADC or network.
 *
 * @param percent    Current battery percentage (0–100, or UNKNOWN).
 * @param file_bytes File size in bytes (from queue entry).
 * @return  true if the upload should be skipped (battery ≤10 % AND
 *          file_bytes > LOW_BATTERY_UPLOAD_MAX_BYTES).
 */
bool battery_should_block_upload(int percent, uint32_t file_bytes);

/* ── Public API (hardware-dependent) ─────────────────────────────────── */

/**
 * @brief Initialise battery monitoring.
 *
 * Creates a FreeRTOS timer that reads VBAT via calibrated ADC every
 * ~15 s, updates the cached level/percent, and publishes
 * RECORDER_EVENT_BATTERY_WARNING / RECORDER_EVENT_BATTERY_CRITICAL
 * on threshold crossings.
 *
 * If the hardware verdict says VBAT is not readable (board.h has no
 * BAT_ADC_PIN or it's NC), this is a no-op and all queries return
 * UNKNOWN / present=false.
 */
void battery_init(void);

/**
 * @brief Stop battery monitoring and release resources.
 */
void battery_deinit(void);

/**
 * @brief Get the cached battery percentage (0–100), or -1 if unknown.
 */
int battery_percent(void);

/**
 * @brief Return true if a charger/USB cable is connected.
 */
bool battery_is_charging(void);

/**
 * @brief Return true if battery monitoring hardware is available.
 */
bool battery_is_present(void);

/**
 * @brief Return true if the battery is at a critical level (≤10 %).
 */
bool battery_is_critical(void);

#ifdef __cplusplus
}
#endif
