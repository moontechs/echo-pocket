/** @file face_registry.cpp
 * @brief Face theme registry implementation.
 */

#include "face_registry.h"
#include "face_plugin.hpp"
#include <string.h>
#include <stdio.h>

/* ESP-IDF logging (available on target and in logic_tests via unity fixture) */
#include "esp_log.h"

static const char *TAG = "face_registry";

/* ── Constants ───────────────────────────────────────────────────────── */

#define MAX_PLUGINS 16

/* ── Registry state ──────────────────────────────────────────────────── */

static FacePlugin *s_plugins[MAX_PLUGINS];
static int         s_plugin_count = 0;

static FacePlugin *s_active = NULL;

static uint32_t s_frame_interval_ms = 50;  /* default 20 fps */
static uint32_t s_last_draw_ms = 0;

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void face_registry_init(void)
{
    s_plugin_count = 0;
    s_active = NULL;
    s_frame_interval_ms = 50;
    s_last_draw_ms = 0;

    for (int i = 0; i < MAX_PLUGINS; i++) {
        s_plugins[i] = NULL;
    }
}

int face_registry_register(FacePlugin *plugin)
{
    if (!plugin) return s_plugin_count;
    if (s_plugin_count >= MAX_PLUGINS) {
        ESP_LOGW(TAG, "Registry full — cannot register plugin '%s'", plugin->id());
        return s_plugin_count;
    }

    s_plugins[s_plugin_count++] = plugin;
    ESP_LOGI(TAG, "Registered face theme '%s' (%s)",
             plugin->id(), plugin->displayName());
    return s_plugin_count;
}

int face_registry_count(void)
{
    return s_plugin_count;
}

/* ── Theme resolution ────────────────────────────────────────────────── */

FacePlugin *face_registry_find(const char *theme_id)
{
    if (!theme_id) return NULL;

    for (int i = 0; i < s_plugin_count; i++) {
        if (strcmp(s_plugins[i]->id(), theme_id) == 0) {
            return s_plugins[i];
        }
    }
    return NULL;
}

void face_registry_begin(const char *theme_id)
{
    FacePlugin *chosen = face_registry_find(theme_id);

    if (!chosen) {
        ESP_LOGW(TAG, "Theme '%s' not found — falling back to 'minimal'", theme_id);
        chosen = face_registry_find("minimal");
    }

    if (!chosen) {
        ESP_LOGE(TAG, "Fallback theme 'minimal' also not found! No face active.");
        s_active = NULL;
        return;
    }

    s_active = chosen;
    s_active->begin();
    /* Reset the frame-rate timer on theme switch so the first frame
     * renders immediately. */
    s_last_draw_ms = 0;

    ESP_LOGI(TAG, "Activated face theme '%s' (%s)",
             s_active->id(), s_active->displayName());
}

FacePlugin *face_registry_get_active(void)
{
    return s_active;
}

/* ── Frame-rate cap ──────────────────────────────────────────────────── */

void face_registry_set_frame_interval_ms(uint32_t interval_ms)
{
    s_frame_interval_ms = interval_ms;
}

bool face_registry_should_update(uint32_t now_ms)
{
    if (s_frame_interval_ms == 0) {
        /* 0 = uncapped (render every call) */
        return true;
    }

    /* Handle wraparound of the monotonic counter. */
    uint32_t elapsed;
    if (now_ms >= s_last_draw_ms) {
        elapsed = now_ms - s_last_draw_ms;
    } else {
        /* Counter wrapped; render now and reset. */
        s_last_draw_ms = now_ms;
        return true;
    }

    if (elapsed >= s_frame_interval_ms) {
        s_last_draw_ms = now_ms;
        return true;
    }

    return false;
}
