/** @file battery.c
 * @brief Battery monitoring — calibrated ADC read, cached level/percent,
 *        threshold-crossing event publishing.
 *
 * Pure logic (discharge curve + threshold) is in battery.h so it can be
 * tested under logic_tests without this file's hardware dependencies.
 */

#include "battery.h"
#include "board.h"
#include "device_events.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/gpio.h"

static const char *TAG = "battery";

/* ── Cached state ────────────────────────────────────────────────────── */

static volatile int   s_percent   = BATTERY_PERCENT_UNKNOWN;
static volatile bool  s_charging  = false;
static volatile bool  s_present   = false;

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static TimerHandle_t             s_timer        = NULL;

/* ── Pure logic implementations ──────────────────────────────────────── */

/**
 * Single-cell Li-ion discharge curve, calibrated for a 3.7 V nominal
 * cell with a conservative knee at the low end.
 *
 * Entries: { voltage_mv, percent } — sorted descending.
 * Values between table points are linearly interpolated.
 * Values above the first entry → 100 %; below the last → 0 %.
 */
static const struct {
    int voltage_mv;
    int percent;
} s_discharge_curve[] = {
    { 4200, 100 },
    { 4100,  95 },
    { 4000,  83 },
    { 3900,  71 },
    { 3850,  64 },
    { 3800,  56 },
    { 3750,  46 },
    { 3700,  36 },
    { 3650,  26 },
    { 3600,  19 },
    { 3550,  13 },
    { 3500,   8 },
    { 3450,   5 },
    { 3400,   3 },
    { 3350,   1 },
    { 3300,   0 },
};

#define DISCHARGE_CURVE_LEN (sizeof(s_discharge_curve) / sizeof(s_discharge_curve[0]))

int battery_voltage_to_percent(int voltage_mv)
{
    if (voltage_mv <= 0) return 0;

    /* Above max → 100 % */
    if (voltage_mv >= s_discharge_curve[0].voltage_mv) return 100;

    /* Below min → 0 % */
    if (voltage_mv <= s_discharge_curve[DISCHARGE_CURVE_LEN - 1].voltage_mv) return 0;

    /* Linear interpolation between two table points */
    for (size_t i = 0; i < DISCHARGE_CURVE_LEN - 1; i++) {
        int v_high = s_discharge_curve[i].voltage_mv;
        int v_low  = s_discharge_curve[i + 1].voltage_mv;

        if (voltage_mv <= v_high && voltage_mv >= v_low) {
            int p_high = s_discharge_curve[i].percent;
            int p_low  = s_discharge_curve[i + 1].percent;

            /* Linear interpolate:  percent = p_low + (p_high - p_low) * (mv - v_low) / (v_high - v_low) */
            int num = (p_high - p_low) * (voltage_mv - v_low);
            int den = v_high - v_low;
            if (den == 0) return p_low;
            return p_low + num / den;
        }
    }

    /* Fallback: shouldn't get here given the boundary checks above */
    return 0;
}

battery_level_t battery_percent_to_threshold(int percent)
{
    if (percent == BATTERY_PERCENT_UNKNOWN) return BATTERY_UNKNOWN;
    if (percent <= 10) return BATTERY_CRITICAL;
    if (percent <= 20) return BATTERY_WARNING;
    return BATTERY_NORMAL;
}

bool battery_should_block_upload(int percent, uint32_t file_bytes)
{
    if (percent == BATTERY_PERCENT_UNKNOWN) return false;
    if (percent > 10) return false;
    return file_bytes > LOW_BATTERY_UPLOAD_MAX_BYTES;
}

/* ── Hardware-dependent ADC read ─────────────────────────────────────── */

/**
 * Read VBAT through the calibrated ADC, apply divider ratio,
 * average BATTERY_ADC_SAMPLES readings, and return millivolts.
 *
 * Returns -1 on error or if hardware is unavailable.
 */
static int battery_read_millivolts(void)
{
    if (!s_adc_handle) return -1;

    int sum = 0;
    int valid = 0;

    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ADC read error: %s", esp_err_to_name(err));
            continue;
        }

        int voltage = 0;
        if (s_cali_handle) {
            err = adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage);
            if (err != ESP_OK) {
                /* Calibration failed — use raw (uncalibrated) conversion as fallback */
                voltage = raw; /* approximate; calibrated is better */
            }
        } else {
            voltage = raw; /* approximate; calibrated is better */
        }

        sum += voltage;
        valid++;
    }

    if (valid == 0) return -1;

    /* Average and apply divider ratio */
    int avg_mv = sum / valid;
    int battery_mv = (int)((float)avg_mv * BATTERY_DIVIDER_RATIO);

    return battery_mv;
}

/* ── Timer callback ──────────────────────────────────────────────────── */

static void battery_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    /* Read charging status (GPIO 3, low = charging) */
    s_charging = (gpio_get_level(BOARD_BAT_CHARGING_PIN) == 0);

    int mv = battery_read_millivolts();
    if (mv < 0) {
        /* Read failed — keep previous percent, don't spam events */
        return;
    }

    int new_percent = battery_voltage_to_percent(mv);
    battery_level_t old_level = battery_percent_to_threshold(s_percent);
    battery_level_t new_level = battery_percent_to_threshold(new_percent);

    s_percent = new_percent;

    /* Publish events on threshold crossings */
    if (new_level != old_level) {
        if (new_level == BATTERY_WARNING) {
            ESP_LOGI(TAG, "Battery warning: %d%% (charging=%s)",
                     new_percent, s_charging ? "yes" : "no");
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_BATTERY_WARNING,
                           NULL, 0, pdMS_TO_TICKS(100));
        } else if (new_level == BATTERY_CRITICAL) {
            ESP_LOGI(TAG, "Battery critical: %d%% (charging=%s)",
                     new_percent, s_charging ? "yes" : "no");
            esp_event_post(RECORDER_EVENTS, RECORDER_EVENT_BATTERY_CRITICAL,
                           NULL, 0, pdMS_TO_TICKS(100));
        } else if (new_level == BATTERY_NORMAL && old_level != BATTERY_UNKNOWN) {
            /* Recovered from warning/critical (charger plugged in) */
            ESP_LOGI(TAG, "Battery recovered to normal: %d%%", new_percent);
        }
    }

    ESP_LOGD(TAG, "Battery: %dmV → %d%% (charging=%s)",
             mv, new_percent, s_charging ? "yes" : "no");
}

/* ── Public API ──────────────────────────────────────────────────────── */

void battery_init(void)
{
    /* Check hardware verdict: is VBAT readable? */
    if (BOARD_BAT_ADC_PIN < 0) {
        ESP_LOGI(TAG, "Battery monitoring unavailable (no ADC pin)");
        s_present = false;
        return;
    }

    /* ── Setup ADC oneshot ────────────────────────────────────── */
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC: %s", esp_err_to_name(err));
        s_present = false;
        return;
    }

    /* Configure channel 0 (GPIO 1) */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATTERY_ADC_ATTEN_DB,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        s_present = false;
        return;
    }

    /* ── Setup ADC calibration (curve-fitting via eFuse) ──────── */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_0,
        .atten    = BATTERY_ADC_ATTEN_DB,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err != ESP_OK) {
        /* Calibration unavailable — log a warning but continue (uncalibrated
         * readings are approximate but usable for threshold decisions). */
        ESP_LOGW(TAG, "ADC calibration not available — using uncalibrated readings");
        s_cali_handle = NULL;
    }

    /* ── Setup charging pin as input ──────────────────────────── */
    gpio_config_t charging_cfg = {
        .pin_bit_mask = (1ULL << BOARD_BAT_CHARGING_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&charging_cfg);

    /* Do an immediate read to populate cached values */
    s_present = true;
    int mv = battery_read_millivolts();
    if (mv >= 0) {
        s_percent = battery_voltage_to_percent(mv);
        s_charging = (gpio_get_level(BOARD_BAT_CHARGING_PIN) == 0);
        battery_level_t level = battery_percent_to_threshold(s_percent);
        ESP_LOGI(TAG, "Battery initialised: %dmV → %d%% (charging=%s, level=%d)",
                 mv, s_percent, s_charging ? "yes" : "no", (int)level);
    } else {
        ESP_LOGW(TAG, "Battery initialised but first read failed");
    }

    /* ── Create periodic timer ────────────────────────────────── */
    s_timer = xTimerCreate(
        "battery_timer",
        pdMS_TO_TICKS(BATTERY_UPDATE_PERIOD_MS),
        pdTRUE,  /* auto-reload */
        NULL,
        battery_timer_cb
    );

    if (!s_timer) {
        ESP_LOGE(TAG, "Failed to create battery timer");
    } else {
        xTimerStart(s_timer, 0);
    }
}

void battery_deinit(void)
{
    if (s_timer) {
        xTimerStop(s_timer, 0);
        xTimerDelete(s_timer, 0);
        s_timer = NULL;
    }

    if (s_cali_handle) {
        adc_cali_delete_scheme_curve_fitting(s_cali_handle);
        s_cali_handle = NULL;
    }

    if (s_adc_handle) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }

    s_present  = false;
    s_percent  = BATTERY_PERCENT_UNKNOWN;
    s_charging = false;
}

int battery_percent(void)
{
    if (!s_present) return BATTERY_PERCENT_UNKNOWN;
    return s_percent;
}

bool battery_is_charging(void)
{
    if (!s_present) return false;
    return s_charging;
}

bool battery_is_present(void)
{
    return s_present;
}

bool battery_is_critical(void)
{
    if (!s_present) return false;
    return s_percent != BATTERY_PERCENT_UNKNOWN && s_percent <= 10;
}
