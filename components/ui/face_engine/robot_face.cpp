/** @file robot_face.cpp
 * @brief Robot face theme — rectangular head, LED matrix eyes, antenna.
 *
 * Square eyes with inner LED segments that light up with voice level.
 * Antenna bounces on VoiceActive.  Status LEDs change color per event.
 * Blink is a horizontal shutter closing over the eyes.
 */

#include "face_plugin.hpp"
#include "display.h"
#include <algorithm>
#include <cstdlib>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────── */

#define ROB_CENTER_X     120
#define ROB_HEAD_Y        30
#define ROB_HEAD_W        160
#define ROB_HEAD_H        150

#define ROB_EYE_Y          80
#define ROB_LEFT_EYE_X     80
#define ROB_RIGHT_EYE_X   140
#define ROB_EYE_SIZE       20  /* square eye half-width */

#define ROB_LED_Y         160
#define ROB_LED_LEFT       60
#define ROB_LED_CENTER    120
#define ROB_LED_RIGHT     180
#define ROB_LED_RADIUS      4

#define ROB_ANTENNA_X     120
#define ROB_ANTENNA_TOP    10
#define ROB_ANTENNA_LEN    20

/* Blink timing (ms) */
#define ROB_BLINK_CLOSE_MS  80
#define ROB_BLINK_HOLD_MS   40
#define ROB_BLINK_OPEN_MS   60

/* Colors */
#define ROB_BG          0x0000  /* black                         */
#define ROB_HEAD_OUTLINE 0x2C6A /* dark teal outline             */
#define ROB_EYE_BG      0x18E3  /* dark green LED off            */
#define ROB_EYE_ON      0x07E0  /* bright green LED on           */
#define ROB_EYE_MAX     0xFFE0  /* yellow at max voice           */
#define ROB_LED_GREEN   0x07E0
#define ROB_LED_RED     0xF800
#define ROB_LED_YELLOW  0xFFE0
#define ROB_LED_BLUE    0x001F
#define ROB_ANTENNA_CLR 0xC618  /* silver                        */
#define ROB_TEXT_CLR    0x8410  /* grey                          */

/* ── Helper: interpolate two RGB565 colors ───────────────────────────── */

static uint16_t rgb565_lerp(uint16_t a, uint16_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int rr = ar + (int)((br - ar) * t);
    int rg = ag + (int)((bg - ag) * t);
    int rb = ab + (int)((bb - ab) * t);
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

/* ── RobotFace class ──────────────────────────────────────────────────── */

class RobotFace : public FacePlugin {
public:
    RobotFace(const FaceConfig &cfg)
        : cfg_(cfg), event_(FaceEvent::Idle),
          eye_segments_(0), target_segments_(0),
          blink_timer_(0), blink_phase_(0),
          next_blink_ms_(4000), anim_timer_(0) {}

    const char* id() const override          { return "robot"; }
    const char* displayName() const override { return "Robot"; }

    void begin() override {
        event_ = FaceEvent::Idle;
        eye_segments_ = 0;
        target_segments_ = 0;
        blink_timer_ = 0;
        blink_phase_ = 0;
        next_blink_ms_ = 4000;
        anim_timer_ = 0;
    }

    void setEvent(FaceEvent event) override {
        event_ = event;
    }

    void update(float voiceLevel, bool /*voiceDetected*/,
                uint32_t deltaMs) override {
        /* ── Target LED segments lit ─────────────────────────────── */
        int max_seg = 4; /* 4 segments per eye */
        target_segments_ = 0;
        if (cfg_.react_to_voice && voiceLevel > 0.01f) {
            float t = voiceLevel;
            if (t > 1.0f) t = 1.0f;
            target_segments_ = (int)(t * max_seg);
            if (target_segments_ < 1 && voiceLevel > 0.01f) target_segments_ = 1;
        }

        /* Smooth segment transitions */
        int delta = target_segments_ - eye_segments_;
        if (delta > 0) eye_segments_++;
        else if (delta < 0) eye_segments_--;

        /* ── Blink animation ────────────────────────────────────── */
        if (cfg_.blink) {
            if (blink_phase_ == 0) {
                if (next_blink_ms_ <= (int32_t)deltaMs) {
                    blink_phase_ = 1;
                    blink_timer_ = 0;
                    next_blink_ms_ = 3000 + (rand() % 4000);
                } else {
                    next_blink_ms_ -= (int32_t)deltaMs;
                }
            } else {
                blink_timer_ += deltaMs;
                if (blink_phase_ == 1 && blink_timer_ >= ROB_BLINK_CLOSE_MS) {
                    blink_phase_ = 2; blink_timer_ = 0;
                } else if (blink_phase_ == 2 && blink_timer_ >= ROB_BLINK_HOLD_MS) {
                    blink_phase_ = 3; blink_timer_ = 0;
                } else if (blink_phase_ == 3 && blink_timer_ >= ROB_BLINK_OPEN_MS) {
                    blink_phase_ = 0; blink_timer_ = 0;
                }
            }
        }

        anim_timer_ += deltaMs;
    }

    void draw() override {
        display_clear(ROB_BG);

        /* ── Head outline ───────────────────────────────────────── */
        display_draw_rect(ROB_CENTER_X - ROB_HEAD_W/2, ROB_HEAD_Y,
                          ROB_HEAD_W, ROB_HEAD_H, ROB_HEAD_OUTLINE);

        /* ── Antenna ────────────────────────────────────────────── */
        int ant_bob = 0;
        if (event_ == FaceEvent::VoiceActive) {
            ant_bob = ((anim_timer_ / 50) % 2) ? 3 : -3;
        }
        display_fill_rect(ROB_ANTENNA_X - 2, ROB_ANTENNA_TOP + ant_bob,
                          4, ROB_ANTENNA_LEN, ROB_ANTENNA_CLR);
        display_fill_circle(ROB_ANTENNA_X, ROB_ANTENNA_TOP + ant_bob,
                            5, ROB_LED_RED);

        /* ── Eyes (square with LED segments) ────────────────────── */
        bool blink_shut = cfg_.blink && (blink_phase_ == 2);
        if (!blink_shut) {
            drawEye(ROB_LEFT_EYE_X, ROB_EYE_Y, eye_segments_);
            drawEye(ROB_RIGHT_EYE_X, ROB_EYE_Y, eye_segments_);
        } else {
            /* Shutter — thin line */
            display_draw_hline(ROB_LEFT_EYE_X - ROB_EYE_SIZE, ROB_EYE_Y,
                               ROB_EYE_SIZE * 2, ROB_EYE_ON);
            display_draw_hline(ROB_RIGHT_EYE_X - ROB_EYE_SIZE, ROB_EYE_Y,
                               ROB_EYE_SIZE * 2, ROB_EYE_ON);
        }

        /* ── Status LEDs ────────────────────────────────────────── */
        drawStatusLEDs();

        /* ── Label ──────────────────────────────────────────────── */
        const char *label = eventLabel();
        if (label) {
            int tw = (int)(strlen(label) * 8);
            display_draw_text(ROB_CENTER_X - tw / 2, 210, label, ROB_TEXT_CLR);
        }
    }

private:
    void drawEye(int cx, int cy, int segments) {
        /* Eye background square */
        int half = ROB_EYE_SIZE;
        display_fill_rect(cx - half, cy - half, half * 2, half * 2, ROB_EYE_BG);
        display_draw_rect(cx - half, cy - half, half * 2, half * 2, ROB_EYE_ON);

        /* Lit segments from center outward */
        if (segments >= 1) {
            uint16_t clr = rgb565_lerp(ROB_EYE_ON, ROB_EYE_MAX,
                                       (float)segments / 4.0f);
            display_fill_rect(cx - 4, cy - 4, 8, 8, clr);
        }
        if (segments >= 2) {
            uint16_t clr = rgb565_lerp(ROB_EYE_ON, ROB_EYE_MAX,
                                       (float)(segments - 1) / 4.0f);
            display_fill_rect(cx - 10, cy - 10, 4, 20, clr);
            display_fill_rect(cx + 6,  cy - 10, 4, 20, clr);
        }
        if (segments >= 3) {
            uint16_t clr = rgb565_lerp(ROB_EYE_ON, ROB_EYE_MAX,
                                       (float)(segments - 2) / 4.0f);
            display_fill_rect(cx - 10, cy - 10, 20, 4, clr);
            display_fill_rect(cx - 10, cy + 6,  20, 4, clr);
        }
        if (segments >= 4) {
            uint16_t clr = rgb565_lerp(ROB_EYE_ON, ROB_EYE_MAX,
                                       (float)(segments - 3) / 4.0f);
            display_fill_rect(cx - half,     cy - half,     half * 2, 3, clr);
            display_fill_rect(cx - half,     cy + half - 3, half * 2, 3, clr);
            display_fill_rect(cx - half,     cy - half, 3, half * 2, clr);
            display_fill_rect(cx + half - 3, cy - half, 3, half * 2, clr);
        }
    }

    void drawStatusLEDs() {
        uint16_t l = ROB_LED_GREEN;
        uint16_t c = ROB_LED_GREEN;
        uint16_t r = ROB_LED_GREEN;

        switch (event_) {
            case FaceEvent::Idle:
                l = ROB_LED_GREEN; c = ROB_LED_GREEN; r = ROB_LED_GREEN;
                break;
            case FaceEvent::Recording:
            case FaceEvent::VoiceActive:
                l = ROB_LED_RED; c = ROB_LED_YELLOW; r = ROB_LED_RED;
                break;
            case FaceEvent::Silence:
                l = ROB_LED_YELLOW; c = ROB_LED_YELLOW; r = ROB_LED_YELLOW;
                break;
            case FaceEvent::Saving:
                l = ROB_LED_BLUE; c = ROB_LED_BLUE; r = ROB_LED_BLUE;
                break;
            case FaceEvent::Uploading:
                l = 0; c = ROB_LED_BLUE; r = 0;
                if ((anim_timer_ / 200) % 2) { l = ROB_LED_BLUE; c = 0; r = ROB_LED_BLUE; }
                break;
            case FaceEvent::UploadSuccess:
                l = ROB_LED_GREEN; c = ROB_LED_GREEN; r = ROB_LED_GREEN;
                break;
            case FaceEvent::UploadError:
                l = ROB_LED_RED; c = ROB_LED_RED; r = ROB_LED_RED;
                break;
            case FaceEvent::LowBattery:
                l = ROB_LED_RED; c = 0; r = 0;
                if ((anim_timer_ / 500) % 2) { c = ROB_LED_RED; }
                break;
            default:
                break;
        }

        display_fill_circle(ROB_LED_LEFT,   ROB_LED_Y, ROB_LED_RADIUS, l);
        display_fill_circle(ROB_LED_CENTER, ROB_LED_Y, ROB_LED_RADIUS, c);
        display_fill_circle(ROB_LED_RIGHT,  ROB_LED_Y, ROB_LED_RADIUS, r);
    }

    const char *eventLabel() const {
        switch (event_) {
            case FaceEvent::Idle:          return "SYS:READY";
            case FaceEvent::Recording:     return "SYS:REC";
            case FaceEvent::VoiceActive:   return "SYS:INPUT";
            case FaceEvent::Silence:       return "SYS:IDLE";
            case FaceEvent::Saving:        return "SYS:WRITE";
            case FaceEvent::Uploading:     return "SYS:TX";
            case FaceEvent::UploadSuccess: return "SYS:OK";
            case FaceEvent::UploadError:   return "SYS:ERR";
            case FaceEvent::LowBattery:    return "SYS:BATLOW";
            default:                       return NULL;
        }
    }

    FaceConfig cfg_;
    FaceEvent  event_;
    int        eye_segments_;
    int        target_segments_;
    uint32_t   blink_timer_;
    int        blink_phase_;
    int32_t    next_blink_ms_;
    uint32_t   anim_timer_;
};

/* ── Factory function for registry ───────────────────────────────────── */

FacePlugin *create_robot_face(const FaceConfig &cfg) {
    return new RobotFace(cfg);
}
