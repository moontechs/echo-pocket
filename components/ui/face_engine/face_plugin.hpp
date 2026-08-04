/** @file face_plugin.hpp
 * @brief FaceEvent enum and FacePlugin abstract class.
 *
 * Defined exactly per AGENTS.md §Face engine:
 *   - FaceEvent: device-state enum the UI task publishes
 *   - FacePlugin: pure-virtual interface every built-in theme implements
 *
 * Themes only receive voiceLevel, voiceDetected, deltaMs, and the current
 * FaceEvent — no access to recorder/network/SD state.  This isolation is
 * enforced by the interface shape itself (no live handles are passed).
 *
 * ESP-IDF C++ builds run with exceptions disabled by default; themes must
 * not rely on exception handling for correctness.
 */

#pragma once

#include <stdint.h>

/* ── FaceEvent ───────────────────────────────────────────────────────── */

enum class FaceEvent {
    Idle,            /**< Home screen, no active recording or upload   */
    Recording,       /**< Recording in progress                       */
    VoiceActive,     /**< Voice activity detected above silence threshold */
    Silence,         /**< No voice activity during recording           */
    Saving,          /**< Finalizing WAV (header patch + fsync)       */
    Uploading,       /**< Upload in progress over network             */
    UploadSuccess,   /**< Upload completed successfully (ok: true)    */
    UploadError,     /**< Upload failed (will be retried)             */
    LowBattery,      /**< Battery ≤ 20 % (warning) or ≤ 10 % (critical) */
};

/** Number of distinct FaceEvent values (for exhaustive-switch tests). */
#define FACE_EVENT_COUNT 9

/* ── FaceConfig (per-theme settings from [face] section) ─────────────── */

struct FaceConfig {
    int  eye_min_size;     /**< Minimum eye radius/height (px)       */
    int  eye_max_size;     /**< Maximum eye radius/height (px)       */
    bool react_to_voice;   /**< Eye size follows voiceLevel           */
    bool blink;            /**< Automatic blink animation enabled     */

    FaceConfig()
        : eye_min_size(5), eye_max_size(22),
          react_to_voice(true), blink(true) {}
};

/* ── FacePlugin (abstract interface) ─────────────────────────────────── */

class FacePlugin {
public:
    virtual ~FacePlugin() = default;

    /** Unique short identifier, e.g. "owl", "minimal", "robot", "pixel".
     *  Must match [face].theme in recorder.ini. */
    virtual const char* id() const = 0;

    /** Human-readable name shown in the Face submenu, e.g. "Owl". */
    virtual const char* displayName() const = 0;

    /** Called once when this theme becomes active (theme switch or boot).
     *  Use for per-theme initialisation. */
    virtual void begin() = 0;

    /** Set the current device-state event.  Called by the UI task whenever
     *  a RECORDER_EVENT arrives. */
    virtual void setEvent(FaceEvent event) = 0;

    /** Advance animation state by @p deltaMs milliseconds.
     *
     *  @p voiceLevel  Post-noise-suppression RMS level in [0.0, 1.0]
     *                  (from voice_level.h pipeline in Task 8).
     *  @p voiceDetected  true when VAD (voice_detection config key) reports
     *                     active speech.
     *  @p deltaMs     Milliseconds since the last update() call. */
    virtual void update(float voiceLevel, bool voiceDetected,
                        uint32_t deltaMs) = 0;

    /** Render the current animation frame to the display.
     *  Called by the UI task, rate-limited by the frame-rate cap. */
    virtual void draw() = 0;
};
