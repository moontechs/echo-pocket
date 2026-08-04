/** @file face_registry.h
 * @brief Face theme registry — stores compiled-in themes, resolves active
 *        theme by id from config, falls back to "minimal" on unknown id,
 *        enforces the frame-rate cap.
 *
 * All functions with "face_registry_" prefix have C linkage so the C
 * UI task (Task 11) can call them directly.  The FacePlugin* types are
 * opaque to C callers.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
class FacePlugin;
extern "C" {
#else
/* Opaque type for C callers */
typedef struct FacePlugin FacePlugin;
#endif

/* ── Lifecycle ───────────────────────────────────────────────────────── */

/** Initialise the registry.  Call once before any other registry function. */
void face_registry_init(void);

/** Register a compiled-in theme.  Returns the new plugin count.
 *  Plugins are stored in insertion order; resolving by id scans the list. */
int face_registry_register(FacePlugin *plugin);

/** Return the number of registered plugins. */
int face_registry_count(void);

/* ── Theme resolution ────────────────────────────────────────────────── */

/** Find a plugin by @p theme_id.  Returns NULL if no match.
 *  Iteration order is insertion order (first registered wins on duplicate id). */
FacePlugin *face_registry_find(const char *theme_id);

/** Resolve and activate the theme named @p theme_id.
 *
 *  If a plugin with that id is registered: calls its begin() and makes it
 *  the active theme.  If no plugin matches: resolves to the "minimal"
 *  plugin instead (hard requirement per AGENTS.md §Face engine).
 *
 *  If even "minimal" is not found (corrupted build), logs an error and
 *  sets active to NULL — the UI task must guard against this. */
void face_registry_begin(const char *theme_id);

/** Return the currently active plugin, or NULL if none selected. */
FacePlugin *face_registry_get_active(void);

/** Register the four built-in themes (owl, minimal, robot, pixel)
 *  with default FaceConfig.  Call once at startup before any
 *  face_registry_begin() call. */
void face_registry_register_defaults(void);

/* ── Frame-rate cap ──────────────────────────────────────────────────── */

/** Set the minimum interval between draw() calls, in milliseconds.
 *  Expected to be 1000 / [face].animation_fps (e.g. 50 ms for 20 fps). */
void face_registry_set_frame_interval_ms(uint32_t interval_ms);

/** Check whether enough time has passed since the last draw() call.
 *
 *  @p now_ms  Monotonic millisecond timestamp (FreeRTOS xTaskGetTickCount
 *             converted to ms, or any other monotonically increasing source).
 *
 *  @return true if the caller should render the next frame, false if the
 *          frame-rate cap says "skip this frame". */
bool face_registry_should_update(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
