#include "battery.h"
#include <stddef.h>

static const struct { int voltage_mv; int percent; } s_discharge_curve[] = {
    {4200,100},{4100,95},{4000,83},{3900,71},{3850,64},{3800,56},
    {3750,46},{3700,36},{3650,26},{3600,19},{3550,13},{3500,8},
    {3450,5},{3400,3},{3350,1},{3300,0},
};
#define DISCHARGE_CURVE_LEN (sizeof(s_discharge_curve) / sizeof(s_discharge_curve[0]))

int battery_voltage_to_percent(int voltage_mv)
{
    if (voltage_mv <= 0) return 0;
    if (voltage_mv >= s_discharge_curve[0].voltage_mv) return 100;
    if (voltage_mv <= s_discharge_curve[DISCHARGE_CURVE_LEN - 1].voltage_mv) return 0;
    for (size_t i = 0; i < DISCHARGE_CURVE_LEN - 1; i++) {
        int vh = s_discharge_curve[i].voltage_mv, vl = s_discharge_curve[i + 1].voltage_mv;
        if (voltage_mv <= vh && voltage_mv >= vl) {
            int ph = s_discharge_curve[i].percent, pl = s_discharge_curve[i + 1].percent;
            return pl + (ph - pl) * (voltage_mv - vl) / (vh - vl);
        }
    }
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
    return percent != BATTERY_PERCENT_UNKNOWN && percent <= 10 && file_bytes > LOW_BATTERY_UPLOAD_MAX_BYTES;
}
