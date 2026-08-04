/** @file owl_face.cpp
 * @brief Owl face theme — expressive round eyes, beak, ear tufts.
 *
 * Eye size scales with voiceLevel between eye_min_size and eye_max_size.
 * Blink animation briefly collapses eyes to thin slits.  Each FaceEvent
 * drives a distinct expression.
 */

#include "face_plugin.hpp"
#include "face_themes.h"
#include "display.h"
#include <algorithm>
#include <cstdlib>
#include <string.h>

/* ── Internal constants ──────────────────────────────────────────────── */

#define OWL_CENTER_X     120
#define OWL_CENTER_Y     100
#define OWL_EYE_Y          80
#define OWL_LEFT_EYE_X     90
#define OWL_RIGHT_EYE_X   150
#define OWL_BEAK_Y        130
#define OWL_EAR_OFFSET     35

/* Blink timing (ms) */
#define BLINK_CLOSE_MS    60
#define BLINK_HOLD_MS     50
#define BLINK_OPEN_MS     60
#define BLINK_INTERVAL_MIN_MS  2000
#define BLINK_INTERVAL_MAX_MS  5000

/* Colors (RGB565) */
#define OWL_BODY_COLOR    0xCE59   /* warm brown          */
#define OWL_EYE_WHITE     0xFFFF   /* white               */
#define OWL_PUPIL_COLOR   0x0000   /* black               */
#define OWL_BEAK_COLOR    0xED20   /* orange              */
#define OWL_EAR_COLOR     0xA514   /* darker brown        */
#define OWL_ALERT_COLOR   0xFFE0   /* yellow (alert eyes) */

/* ── OwlFace class ───────────────────────────────────────────────────── */

class OwlFace : public FacePlugin {
public:
    OwlFace(const FaceConfig &cfg)
        : cfg_(cfg), event_(FaceEvent::Idle),
          eye_size_(cfg.eye_min_size),
          blink_timer_(0), blink_phase_(0), /* 0=open,1=closing,2=held,3=opening */
          next_blink_ms_(2000),
          anim_timer_(0), alert_flash_(0) {}

    const char* id() const override          { return "owl"; }
    const char* displayName() const override { return "Owl"; }

    void begin() override {
        event_ = FaceEvent::Idle;
        eye_size_ = cfg_.eye_min_size;
        blink_timer_ = 0;
        blink_phase_ = 0;
        anim_timer_ = 0;
        alert_flash_ = 0;
        next_blink_ms_ = 2000 + (rand() % 3000);
    }

    void setEvent(FaceEvent event) override {
        event_ = event;
        anim_timer_ = 0;
        /* Transitions: flash alert on VoiceActive, droop on LowBattery */
        if (event == FaceEvent::VoiceActive) alert_flash_ = 300;
        if (event == FaceEvent::LowBattery)  alert_flash_ = 0;
    }

    void update(float voiceLevel, bool /*voiceDetected*/,
                uint32_t deltaMs) override {
        /* ── Target eye size from voice level ────────────────────── */
        int target = cfg_.eye_min_size;
        if (cfg_.react_to_voice && voiceLevel > 0.0f) {
            float t = voiceLevel; /* already in [0,1] */
            if (t > 1.0f) t = 1.0f;
            target = cfg_.eye_min_size +
                     (int)((cfg_.eye_max_size - cfg_.eye_min_size) * t);
        }

        /* Smooth toward target over ~100 ms */
        int delta = target - eye_size_;
        if (delta != 0) {
            int step = (int)(delta * (float)deltaMs / 100.0f);
            if (step == 0) step = (delta > 0) ? 1 : -1;
            eye_size_ += step;
            if ((delta > 0 && eye_size_ > target) ||
                (delta < 0 && eye_size_ < target))
                eye_size_ = target;
        }

        /* ── Blink animation ─────────────────────────────────────── */
        if (cfg_.blink) {
            if (blink_phase_ == 0) {
                /* Waiting for next blink */
                if (next_blink_ms_ <= (int32_t)deltaMs) {
                    blink_phase_ = 1;
                    blink_timer_ = 0;
                    next_blink_ms_ = BLINK_INTERVAL_MIN_MS +
                                     (rand() % (BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS));
                } else {
                    next_blink_ms_ -= (int32_t)deltaMs;
                }
            } else {
                blink_timer_ += deltaMs;
                if (blink_phase_ == 1 && blink_timer_ >= BLINK_CLOSE_MS) {
                    blink_phase_ = 2; blink_timer_ = 0;
                } else if (blink_phase_ == 2 && blink_timer_ >= BLINK_HOLD_MS) {
                    blink_phase_ = 3; blink_timer_ = 0;
                } else if (blink_phase_ == 3 && blink_timer_ >= BLINK_OPEN_MS) {
                    blink_phase_ = 0; blink_timer_ = 0;
                }
            }
        }

        anim_timer_ += deltaMs;
        if (alert_flash_ > 0) {
            alert_flash_ -= (int32_t)deltaMs;
            if (alert_flash_ < 0) alert_flash_ = 0;
        }
    }

    void draw() override {
        display_clear(0x0000); /* black background */

        int eye_r = eye_size_;
        if (eye_r < 3) eye_r = 3;

        /* ── Ear tufts ──────────────────────────────────────────── */
        display_fill_rect(OWL_CENTER_X - OWL_EAR_OFFSET - 10, 10, 20, 30, OWL_EAR_COLOR);
        display_fill_rect(OWL_CENTER_X + OWL_EAR_OFFSET - 10, 10, 20, 30, OWL_EAR_COLOR);

        /* ── Body (circle behind eyes and beak) ─────────────────── */
        display_fill_circle(OWL_CENTER_X, OWL_CENTER_Y + 40, 80, OWL_BODY_COLOR);

        /* ── Eyes ───────────────────────────────────────────────── */
        int effective_r = eye_r;
        bool blinking = cfg_.blink && (blink_phase_ == 2);
        bool mid_blink = cfg_.blink && (blink_phase_ == 1 || blink_phase_ == 3);
        float blink_fraction = 1.0f;
        if (mid_blink) {
            if (blink_phase_ == 1)
                blink_fraction = 1.0f - (float)blink_timer_ / BLINK_CLOSE_MS;
            else
                blink_fraction = (float)blink_timer_ / BLINK_OPEN_MS;
        }

        uint16_t eye_color = OWL_EYE_WHITE;
        if (alert_flash_ > 0 && (anim_timer_ / 150) % 2 == 0) {
            eye_color = OWL_ALERT_COLOR;
        }

        /* Left eye */
        if (blinking) {
            display_fill_rect(OWL_LEFT_EYE_X - effective_r, OWL_EYE_Y - 2,
                              effective_r * 2, 4, eye_color);
        } else {
            int draw_r = (int)(effective_r * blink_fraction);
            if (draw_r < 3) draw_r = 3;
            display_fill_circle(OWL_LEFT_EYE_X, OWL_EYE_Y, draw_r, eye_color);
            /* Pupil */
            int pupil_r = draw_r / 2;
            if (pupil_r < 1) pupil_r = 1;
            display_fill_circle(OWL_LEFT_EYE_X, OWL_EYE_Y, pupil_r, OWL_PUPIL_COLOR);
        }

        /* Right eye */
        if (blinking) {
            display_fill_rect(OWL_RIGHT_EYE_X - effective_r, OWL_EYE_Y - 2,
                              effective_r * 2, 4, eye_color);
        } else {
            int draw_r = (int)(effective_r * blink_fraction);
            if (draw_r < 3) draw_r = 3;
            display_fill_circle(OWL_RIGHT_EYE_X, OWL_EYE_Y, draw_r, eye_color);
            int pupil_r = draw_r / 2;
            if (pupil_r < 1) pupil_r = 1;
            display_fill_circle(OWL_RIGHT_EYE_X, OWL_EYE_Y, pupil_r, OWL_PUPIL_COLOR);
        }

        /* ── Beak ───────────────────────────────────────────────── */
        int beak_w = eye_r / 2;
        if (beak_w < 4) beak_w = 4;
        for (int row = 0; row < beak_w; row++) {
            int span = row * 2 + 1;
            display_draw_hline(OWL_CENTER_X - span / 2, OWL_BEAK_Y + row, span, OWL_BEAK_COLOR);
        }

        /* ── Status label ────────────────────────────────────────── */
        const char *label = eventLabel();
        if (label) {
            int tw = (int)(strlen(label) * 8);
            display_draw_text(OWL_CENTER_X - tw / 2, 210, label, 0x8410);
        }
    }

private:
    const char *eventLabel() const {
        switch (event_) {
            case FaceEvent::Idle:          return "Ready";
            case FaceEvent::Recording:     return "* REC *";
            case FaceEvent::VoiceActive:   return "Speaking";
            case FaceEvent::Silence:       return "Silent...";
            case FaceEvent::Saving:        return "Saving...";
            case FaceEvent::Uploading:     return "Uploading";
            case FaceEvent::UploadSuccess: return "Sent!";
            case FaceEvent::UploadError:   return "Retry...";
            case FaceEvent::LowBattery:    return "Low Batt";
            default:                       return NULL;
        }
    }

    FaceConfig cfg_;
    FaceEvent  event_;
    int        eye_size_;
    uint32_t   blink_timer_;
    int        blink_phase_;
    int32_t    next_blink_ms_;
    uint32_t   anim_timer_;
    int32_t    alert_flash_;
};

/* ── Factory function for registry ───────────────────────────────────── */

FacePlugin *create_owl_face(const FaceConfig &cfg) {
    return new OwlFace(cfg);
}
