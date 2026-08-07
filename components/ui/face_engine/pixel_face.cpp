/** @file pixel_face.cpp
 * @brief Pixel face theme — retro 8-bit game style, blocky eyes.
 *
 * Eyes are built from pixel blocks (filled rects).  Voice level adds
 * more pixel blocks to the eye pattern.  Blink is a pixel dissolve.
 * Each FaceEvent switches the color palette.
 */

#include "face_plugin.hpp"
#include "face_themes.h"
#include "display.h"
#include <algorithm>
#include <cstdlib>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────── */

#define PX_CENTER_X      120
#define PX_EYE_Y          85
#define PX_LEFT_EYE_X     85
#define PX_RIGHT_EYE_X   145
#define PX_BLOCK_SIZE      6   /* pixel block edge in display px */

#define PX_MOUTH_Y       135

/* Palette per event */
#define PX_C_IDLE        0x07E0  /* green                     */
#define PX_C_REC         0xF800  /* red                       */
#define PX_C_VOICE       0xFFE0  /* yellow                    */
#define PX_C_SILENCE     0x8410  /* grey                      */
#define PX_C_SAVE        0x001F  /* blue                      */
#define PX_C_UPLOAD      0x07FF  /* cyan                      */
#define PX_C_SUCCESS     0x07E0  /* green                     */
#define PX_C_ERROR       0xF800  /* red                       */
#define PX_C_BATLOW      0xF800  /* red flashing              */
#define PX_BG            0x0000  /* black                     */
#define PX_TEXT          0x8410

/* Blink */
#define PX_BLINK_CLOSE_MS  50
#define PX_BLINK_HOLD_MS   40
#define PX_BLINK_OPEN_MS   50

/* ── PixelFace class ──────────────────────────────────────────────────── */

class PixelFace : public FacePlugin {
public:
    PixelFace(const FaceConfig &cfg)
        : cfg_(cfg), event_(FaceEvent::Idle),
          voice_level_(0.0f),
          blink_timer_(0), blink_phase_(0),
          next_blink_ms_(2500), anim_timer_(0) {}

    const char* id() const override          { return "pixel"; }
    const char* displayName() const override { return "Pixel"; }

    void begin() override {
        event_ = FaceEvent::Idle;
        voice_level_ = 0.0f;
        blink_timer_ = 0;
        blink_phase_ = 0;
        next_blink_ms_ = 2500;
        anim_timer_ = 0;
    }

    void setEvent(FaceEvent event) override {
        event_ = event;
    }

    void update(float voiceLevel, bool /*voiceDetected*/,
                uint32_t deltaMs) override {
        voice_level_ = voiceLevel;
        if (voice_level_ > 1.0f) voice_level_ = 1.0f;

        /* ── Blink animation ────────────────────────────────────── */
        if (cfg_.blink) {
            if (blink_phase_ == 0) {
                if (next_blink_ms_ <= (int32_t)deltaMs) {
                    blink_phase_ = 1;
                    blink_timer_ = 0;
                    next_blink_ms_ = 2500 + (rand() % 3500);
                } else {
                    next_blink_ms_ -= (int32_t)deltaMs;
                }
            } else {
                blink_timer_ += deltaMs;
                if (blink_phase_ == 1 && blink_timer_ >= PX_BLINK_CLOSE_MS) {
                    blink_phase_ = 2; blink_timer_ = 0;
                } else if (blink_phase_ == 2 && blink_timer_ >= PX_BLINK_HOLD_MS) {
                    blink_phase_ = 3; blink_timer_ = 0;
                } else if (blink_phase_ == 3 && blink_timer_ >= PX_BLINK_OPEN_MS) {
                    blink_phase_ = 0; blink_timer_ = 0;
                }
            }
        }

        anim_timer_ += deltaMs;
    }

    void draw() override {
        display_clear(PX_BG);

        uint16_t pal = palette();

        /* ── Eye pixel patterns ─────────────────────────────────── */
        int eye_blocks = cfg_.react_to_voice
            ? (int)(1 + voice_level_ * 3)   /* 1–4 blocks */
            : 2;

        bool blink_shut = cfg_.blink && (blink_phase_ == 2);

        drawPixelEye(PX_LEFT_EYE_X,  PX_EYE_Y, eye_blocks, blink_shut, pal);
        drawPixelEye(PX_RIGHT_EYE_X, PX_EYE_Y, eye_blocks, blink_shut, pal);

        /* ── Mouth (pixel line) ─────────────────────────────────── */
        drawPixelMouth(pal);

        /* ── Label ──────────────────────────────────────────────── */
        const char *label = eventLabel();
        if (label) {
            int tw = (int)(strlen(label) * 8);
            display_draw_text(PX_CENTER_X - tw / 2, 210, label, PX_TEXT);
        }
    }

private:
    /** Draw a pixel-art eye as a grid of PX_BLOCK_SIZE blocks.
     *  Pattern is a 3x3 grid; lit blocks depend on voice level. */
    void drawPixelEye(int cx, int cy, int blocks, bool blink_shut, uint16_t color) {
        int b = PX_BLOCK_SIZE;
        /* 3x3 grid centered at (cx, cy) */
        /* Block positions: centre is always lit, corners fill in */

        if (blink_shut) {
            /* Single horizontal line */
            display_fill_rect(cx - b - b/2, cy - b/2, b * 3, b, color);
            return;
        }

        /* Always lit: center block */
        display_fill_rect(cx - b/2, cy - b/2, b, b, color);

        if (blocks >= 2) {
            /* Top and bottom centers */
            display_fill_rect(cx - b/2, cy - b - b/2,     b, b, color);
            display_fill_rect(cx - b/2, cy + b - b/2,     b, b, color);
        }
        if (blocks >= 3) {
            /* Left and right centers */
            display_fill_rect(cx - b - b/2, cy - b/2,     b, b, color);
            display_fill_rect(cx + b - b/2, cy - b/2,     b, b, color);
        }
        if (blocks >= 4) {
            /* Corners */
            display_fill_rect(cx - b - b/2, cy - b - b/2, b, b, color);
            display_fill_rect(cx + b - b/2, cy - b - b/2, b, b, color);
            display_fill_rect(cx - b - b/2, cy + b - b/2, b, b, color);
            display_fill_rect(cx + b - b/2, cy + b - b/2, b, b, color);
        }
    }

    void drawPixelMouth(uint16_t color) {
        int b = PX_BLOCK_SIZE;
        switch (event_) {
            case FaceEvent::Idle:
            case FaceEvent::Silence:
                /* Neutral: 3-block line */
                display_fill_rect(PX_CENTER_X - b - b/2, PX_MOUTH_Y, b * 3, b, color);
                break;
            case FaceEvent::Recording:
            case FaceEvent::VoiceActive:
                /* Open: 5-block U-shape */
                display_fill_rect(PX_CENTER_X - b * 2 - b/2, PX_MOUTH_Y,       b, b, color);
                display_fill_rect(PX_CENTER_X - b - b/2,     PX_MOUTH_Y + b,   b, b, color);
                display_fill_rect(PX_CENTER_X - b/2,         PX_MOUTH_Y + b,   b, b, color);
                display_fill_rect(PX_CENTER_X + b - b/2,     PX_MOUTH_Y + b,   b, b, color);
                display_fill_rect(PX_CENTER_X + b * 2 - b/2, PX_MOUTH_Y,       b, b, color);
                break;
            case FaceEvent::UploadSuccess:
                /* Smile: curving up */
                display_fill_rect(PX_CENTER_X - b * 2 - b/2, PX_MOUTH_Y - b,   b, b, color);
                display_fill_rect(PX_CENTER_X - b - b/2,     PX_MOUTH_Y,       b, b, color);
                display_fill_rect(PX_CENTER_X - b/2,         PX_MOUTH_Y,       b, b, color);
                display_fill_rect(PX_CENTER_X + b - b/2,     PX_MOUTH_Y,       b, b, color);
                display_fill_rect(PX_CENTER_X + b * 2 - b/2, PX_MOUTH_Y - b,   b, b, color);
                break;
            case FaceEvent::UploadError:
            case FaceEvent::LowBattery:
                /* Frown */
                display_fill_rect(PX_CENTER_X - b * 2 - b/2, PX_MOUTH_Y,       b, b, color);
                display_fill_rect(PX_CENTER_X - b - b/2,     PX_MOUTH_Y - b,   b, b, color);
                display_fill_rect(PX_CENTER_X - b/2,         PX_MOUTH_Y - b,   b, b, color);
                display_fill_rect(PX_CENTER_X + b - b/2,     PX_MOUTH_Y - b,   b, b, color);
                display_fill_rect(PX_CENTER_X + b * 2 - b/2, PX_MOUTH_Y,       b, b, color);
                break;
            default:
                display_fill_rect(PX_CENTER_X - b - b/2, PX_MOUTH_Y, b * 3, b, color);
                break;
        }
    }

    uint16_t palette() const {
        switch (event_) {
            case FaceEvent::Idle:          return PX_C_IDLE;
            case FaceEvent::Recording:     return PX_C_REC;
            case FaceEvent::VoiceActive:   return PX_C_VOICE;
            case FaceEvent::Silence:       return PX_C_SILENCE;
            case FaceEvent::Saving:        return PX_C_SAVE;
            case FaceEvent::Uploading:     return PX_C_UPLOAD;
            case FaceEvent::UploadSuccess: return PX_C_SUCCESS;
            case FaceEvent::UploadError:   return PX_C_ERROR;
            case FaceEvent::LowBattery:
                /* Flash red */
                return ((anim_timer_ / 300) % 2) ? PX_C_ERROR : PX_BG;
            default:                       return PX_C_IDLE;
        }
    }

    const char *eventLabel() const {
        return face_event_label(event_);
    }

    FaceConfig cfg_;
    FaceEvent  event_;
    float      voice_level_;
    uint32_t   blink_timer_;
    int        blink_phase_;
    int32_t    next_blink_ms_;
    uint32_t   anim_timer_;
};

/* ── Factory function for registry ───────────────────────────────────── */

FacePlugin *create_pixel_face(const FaceConfig &cfg) {
    return new PixelFace(cfg);
}
