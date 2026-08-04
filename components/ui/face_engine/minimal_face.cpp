/** @file minimal_face.cpp
 * @brief Minimal face theme — two dots + line mouth, ultra-low CPU.
 *
 * The fallback theme when an unknown theme id is in config.  Keeps
 * drawing as simple as possible: eye circles scale subtly with voice,
 * mouth line changes shape per event.  No blink animation overhead.
 */

#include "face_plugin.hpp"
#include "face_themes.h"
#include "display.h"
#include <algorithm>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────── */

#define MIN_CENTER_X    120
#define MIN_EYE_Y         80
#define MIN_LEFT_X        95
#define MIN_RIGHT_X      145
#define MIN_MOUTH_Y      135

/* Colors */
#define MIN_BG          0x0000  /* black background         */
#define MIN_EYE_COLOR   0xFFFF  /* white                    */
#define MIN_MOUTH_COLOR 0x8410  /* grey                     */
#define MIN_REC_COLOR   0xF800  /* red (recording dot)      */
#define MIN_WARN_COLOR  0xFFE0  /* yellow (warning)         */

/* ── MinimalFace class ───────────────────────────────────────────────── */

class MinimalFace : public FacePlugin {
public:
    MinimalFace(const FaceConfig &cfg)
        : cfg_(cfg), event_(FaceEvent::Idle),
          eye_size_(cfg.eye_min_size), blink_timer_(0),
          blink_active_(false), anim_timer_(0) {}

    const char* id() const override          { return "minimal"; }
    const char* displayName() const override { return "Minimal"; }

    void begin() override {
        event_ = FaceEvent::Idle;
        eye_size_ = cfg_.eye_min_size;
        blink_timer_ = 0;
        blink_active_ = false;
        anim_timer_ = 0;
    }

    void setEvent(FaceEvent event) override {
        event_ = event;
    }

    void update(float voiceLevel, bool /*voiceDetected*/,
                uint32_t deltaMs) override {
        /* ── Target eye size ────────────────────────────────────── */
        int target = cfg_.eye_min_size;
        if (cfg_.react_to_voice && voiceLevel > 0.0f) {
            float t = voiceLevel;
            if (t > 1.0f) t = 1.0f;
            target = cfg_.eye_min_size +
                     (int)((cfg_.eye_max_size - cfg_.eye_min_size) * t);
        }

        int delta = target - eye_size_;
        if (delta != 0) {
            int step = (int)(delta * (float)deltaMs / 100.0f);
            if (step == 0) step = (delta > 0) ? 1 : -1;
            eye_size_ += step;
            if ((delta > 0 && eye_size_ > target) ||
                (delta < 0 && eye_size_ < target))
                eye_size_ = target;
        }

        /* ── Blink (simple on/off ────────────────────────────────── */
        if (cfg_.blink) {
            blink_timer_ += deltaMs;
            if (!blink_active_ && blink_timer_ >= 3000) {
                blink_active_ = true;
                blink_timer_ = 0;
            } else if (blink_active_ && blink_timer_ >= 150) {
                blink_active_ = false;
                blink_timer_ = 0;
            }
        }

        anim_timer_ += deltaMs;
    }

    void draw() override {
        display_clear(MIN_BG);

        int r = eye_size_;
        if (r < 2) r = 2;

        /* ── Recording indicator ────────────────────────────────── */
        if (event_ == FaceEvent::Recording || event_ == FaceEvent::VoiceActive) {
            display_fill_circle(30, 30, 5, MIN_REC_COLOR);
        }

        /* ── Eyes ───────────────────────────────────────────────── */
        if (!blink_active_) {
            display_fill_circle(MIN_LEFT_X,  MIN_EYE_Y, r, MIN_EYE_COLOR);
            display_fill_circle(MIN_RIGHT_X, MIN_EYE_Y, r, MIN_EYE_COLOR);
        }

        /* ── Mouth (varies with event) ──────────────────────────── */
        mouthDraw();

        /* ── Status label ───────────────────────────────────────── */
        const char *label = eventLabel();
        if (label) {
            int tw = (int)(strlen(label) * 8);
            display_draw_text(MIN_CENTER_X - tw / 2, 200, label, 0x8410);
        }
    }

private:
    void mouthDraw() {
        switch (event_) {
            case FaceEvent::Idle:
                /* Neutral line */
                display_draw_hline(MIN_CENTER_X - 10, MIN_MOUTH_Y, 20, MIN_MOUTH_COLOR);
                break;
            case FaceEvent::Recording:
            case FaceEvent::VoiceActive:
                /* Open circle */
                display_fill_circle(MIN_CENTER_X, MIN_MOUTH_Y, 5, MIN_MOUTH_COLOR);
                break;
            case FaceEvent::Silence:
                /* Small line */
                display_draw_hline(MIN_CENTER_X - 6, MIN_MOUTH_Y, 12, MIN_MOUTH_COLOR);
                break;
            case FaceEvent::UploadSuccess:
                /* Smile arc - simple upturned line */
                display_draw_hline(MIN_CENTER_X - 8, MIN_MOUTH_Y - 3, 16, MIN_MOUTH_COLOR);
                break;
            case FaceEvent::UploadError:
            case FaceEvent::LowBattery:
                /* Frown */
                display_draw_hline(MIN_CENTER_X - 8, MIN_MOUTH_Y + 3, 16, MIN_MOUTH_COLOR);
                break;
            default:
                display_draw_hline(MIN_CENTER_X - 10, MIN_MOUTH_Y, 20, MIN_MOUTH_COLOR);
                break;
        }
    }

    const char *eventLabel() const {
        switch (event_) {
            case FaceEvent::Idle:          return "Ready";
            case FaceEvent::Recording:     return "REC";
            case FaceEvent::VoiceActive:   return "...";
            case FaceEvent::Saving:        return "Save";
            case FaceEvent::Uploading:     return "Send";
            case FaceEvent::UploadSuccess: return "OK";
            case FaceEvent::UploadError:   return "Err";
            case FaceEvent::LowBattery:    return "Bat!";
            default:                       return NULL;
        }
    }

    FaceConfig cfg_;
    FaceEvent  event_;
    int        eye_size_;
    uint32_t   blink_timer_;
    bool       blink_active_;
    uint32_t   anim_timer_;
};

/* ── Factory function for registry ───────────────────────────────────── */

FacePlugin *create_minimal_face(const FaceConfig &cfg) {
    return new MinimalFace(cfg);
}
