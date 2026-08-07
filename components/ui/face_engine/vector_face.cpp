/** @file vector_face.cpp
 * @brief Vector face theme — blocky rectangular eyes with brow lines,
 *        inspired by the Anki Vector / "cute robot" LCD face style.
 *
 * Two solid rectangle eyes, each with a straight brow line above it, a
 * mouth line/arc below, and an event label at the bottom. Eyes squash to
 * a thin bar on blink and grow slightly with voice level.
 */

#include "face_plugin.hpp"
#include "face_themes.h"
#include "display.h"
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────── */

#define VEC_CENTER_X    120
#define VEC_EYE_Y        87   /* top of eye rect at rest             */
#define VEC_EYE_W        64
#define VEC_EYE_H        50
#define VEC_EYE_GAP      16   /* gap between the two eyes            */
#define VEC_EYE_RADIUS   14   /* eye corner rounding                 */
#define VEC_BROW_OFFSET   8   /* brow line above eye top             */
#define VEC_MOUTH_Y     172
#define VEC_LABEL_Y     205

#define VEC_LEFT_X   (VEC_CENTER_X - VEC_EYE_GAP / 2 - VEC_EYE_W)
#define VEC_RIGHT_X  (VEC_CENTER_X + VEC_EYE_GAP / 2)

/* Blink timing (ms) — squash-hold-open, same shape as robot_face's shutter */
#define VEC_BLINK_CLOSE_MS  70
#define VEC_BLINK_HOLD_MS   40
#define VEC_BLINK_OPEN_MS   70
#define VEC_BLINK_BAR_H       6

/* Colors */
#define VEC_BG          0x0000  /* black                        */
#define VEC_EYE_NORMAL  0x07E6  /* mint green                   */
#define VEC_EYE_ACTIVE  0x07FF  /* cyan                         */
#define VEC_EYE_WARN    0xFD20  /* orange                       */
#define VEC_EYE_ERROR   0xF800  /* red                          */
#define VEC_BROW_COLOR  0xFFFF  /* white                        */
#define VEC_MOUTH_COLOR 0xFFFF  /* white                        */
#define VEC_LABEL_COLOR 0x07E6  /* mint green                   */

/* ── VectorFace class ───────────────────────────────────────────────── */

class VectorFace : public FacePlugin {
public:
    VectorFace(const FaceConfig &cfg)
        : cfg_(cfg), event_(FaceEvent::Idle),
          eye_h_(VEC_EYE_H), blink_timer_(0), blink_phase_(0),
          next_blink_ms_(3500), anim_timer_(0) {}

    const char* id() const override          { return "vector"; }
    const char* displayName() const override { return "Vector"; }

    void begin() override {
        event_ = FaceEvent::Idle;
        eye_h_ = VEC_EYE_H;
        blink_timer_ = 0;
        blink_phase_ = 0;
        next_blink_ms_ = 3500;
        anim_timer_ = 0;
    }

    void setEvent(FaceEvent event) override {
        event_ = event;
    }

    void update(float voiceLevel, bool /*voiceDetected*/,
                uint32_t deltaMs) override {
        /* ── Eye height follows voice level ─────────────────────── */
        int target = VEC_EYE_H;
        if (cfg_.react_to_voice && voiceLevel > 0.0f) {
            float t = voiceLevel;
            if (t > 1.0f) t = 1.0f;
            target = VEC_EYE_H + (int)(14.0f * t);
        }
        int delta = target - eye_h_;
        if (delta != 0) {
            int step = (int)(delta * (float)deltaMs / 80.0f);
            if (step == 0) step = (delta > 0) ? 1 : -1;
            eye_h_ += step;
            if ((delta > 0 && eye_h_ > target) || (delta < 0 && eye_h_ < target))
                eye_h_ = target;
        }

        /* ── Blink animation ────────────────────────────────────── */
        if (cfg_.blink) {
            if (blink_phase_ == 0) {
                if (next_blink_ms_ <= (int32_t)deltaMs) {
                    blink_phase_ = 1;
                    blink_timer_ = 0;
                    next_blink_ms_ = 3000 + (int32_t)(anim_timer_ % 4000);
                } else {
                    next_blink_ms_ -= (int32_t)deltaMs;
                }
            } else {
                blink_timer_ += deltaMs;
                if (blink_phase_ == 1 && blink_timer_ >= VEC_BLINK_CLOSE_MS) {
                    blink_phase_ = 2; blink_timer_ = 0;
                } else if (blink_phase_ == 2 && blink_timer_ >= VEC_BLINK_HOLD_MS) {
                    blink_phase_ = 3; blink_timer_ = 0;
                } else if (blink_phase_ == 3 && blink_timer_ >= VEC_BLINK_OPEN_MS) {
                    blink_phase_ = 0; blink_timer_ = 0;
                }
            }
        }

        anim_timer_ += deltaMs;
    }

    void draw() override {
        display_clear(VEC_BG);

        uint16_t eye_color = eyeColor();
        bool blink_shut = cfg_.blink && (blink_phase_ == 2);
        int h = blink_shut ? VEC_BLINK_BAR_H : eye_h_;
        int y = VEC_EYE_Y + (VEC_EYE_H - h) / 2; /* keep eyes vertically centered */

        /* Eyes are sharp rectangles except right after a successful send,
         * when they round off to match the smiling mouth. */
        int radius = (event_ == FaceEvent::UploadSuccess) ? VEC_EYE_RADIUS : 0;
        display_fill_rounded_rect(VEC_LEFT_X,  y, VEC_EYE_W, h, radius, eye_color);
        display_fill_rounded_rect(VEC_RIGHT_X, y, VEC_EYE_W, h, radius, eye_color);

        /* Brow lines track the eye's current top (y), not the resting
         * position — so they move down with the eye on blink/squash,
         * matching the reference face. Flat for every state, including
         * UploadSuccess — the reference video's post-send face keeps
         * flat brows too, only the mouth changes to a smile. */
        int brow_y = y - VEC_BROW_OFFSET;
        display_draw_hline(VEC_LEFT_X,  brow_y, VEC_EYE_W, VEC_BROW_COLOR);
        display_draw_hline(VEC_RIGHT_X, brow_y, VEC_EYE_W, VEC_BROW_COLOR);

        mouthDraw();

        const char *label = eventLabel();
        if (label) {
            int tw = (int)(strlen(label) * 8);
            display_draw_text(VEC_CENTER_X - tw / 2, VEC_LABEL_Y, label, VEC_LABEL_COLOR);
        }
    }

private:
    uint16_t eyeColor() const {
        switch (event_) {
            case FaceEvent::Recording:
            case FaceEvent::VoiceActive:
            case FaceEvent::Saving:
            case FaceEvent::Uploading:
                return VEC_EYE_ACTIVE;
            case FaceEvent::UploadSuccess:
                return VEC_EYE_NORMAL;
            case FaceEvent::UploadError:
                return VEC_EYE_ERROR;
            case FaceEvent::LowBattery:
                return VEC_EYE_WARN;
            default:
                return VEC_EYE_NORMAL;
        }
    }

    void mouthDraw() {
        switch (event_) {
            case FaceEvent::UploadSuccess:
                /* Smile ⌣ */
                display_draw_smile_arc(VEC_CENTER_X, VEC_MOUTH_Y - 10, 32, 16, 3, VEC_MOUTH_COLOR);
                break;
            case FaceEvent::UploadError:
            case FaceEvent::LowBattery:
                /* Frown — flat line only, no inverted-arc primitive */
                display_draw_hline(VEC_CENTER_X - 14, VEC_MOUTH_Y + 6, 28, VEC_MOUTH_COLOR);
                break;
            default:
                display_draw_hline(VEC_CENTER_X - 14, VEC_MOUTH_Y, 28, VEC_MOUTH_COLOR);
                break;
        }
    }

    const char *eventLabel() const {
        return face_event_label(event_);
    }

    FaceConfig cfg_;
    FaceEvent  event_;
    int        eye_h_;
    uint32_t   blink_timer_;
    int        blink_phase_;
    int32_t    next_blink_ms_;
    uint32_t   anim_timer_;
};

/* ── Factory function for registry ───────────────────────────────────── */

FacePlugin *create_vector_face(const FaceConfig &cfg) {
    return new VectorFace(cfg);
}
