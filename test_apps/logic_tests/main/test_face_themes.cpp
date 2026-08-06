/** @file test_face_themes.cpp
 * @brief Table-driven smoke tests for all 4 built-in face themes (Task 10).
 *
 * Each test:
 *   1. Creates a theme with default FaceConfig
 *   2. Calls setEvent() for every FaceEvent value
 *   3. Calls update() with synthetic voice levels
 *   4. Calls draw() and verifies it produced display output
 *   5. Verifies id() and displayName() are non-empty
 *
 * The display stubs (test_display_stubs.c) count draw calls — we assert
 * that draw() actually invokes the display primitives (smoke test, not
 * pixel-perfect).
 */

#include "unity.h"
#include "face_plugin.hpp"
#include "face_themes.h"
#include <string.h>

/* ── External counters from test_display_stubs.c ─────────────────────── */

extern int g_stub_clear_count;
extern int g_stub_text_count;
extern int g_stub_fill_rect_count;
extern int g_stub_draw_rect_count;
extern int g_stub_fill_circle_count;
extern int g_stub_hline_count;

extern "C" void stub_display_reset_counters(void);

/* ── Helper: total draw calls ────────────────────────────────────────── */

static int total_draw_calls(void)
{
    return g_stub_clear_count + g_stub_text_count +
           g_stub_fill_rect_count + g_stub_draw_rect_count +
           g_stub_fill_circle_count + g_stub_hline_count;
}

/* ── Helper: test one theme through all events ───────────────────────── */

static void exercise_theme(FacePlugin *theme)
{
    TEST_ASSERT_NOT_NULL(theme);
    TEST_ASSERT_NOT_NULL(theme->id());
    TEST_ASSERT_NOT_NULL(theme->displayName());
    TEST_ASSERT_TRUE(strlen(theme->id()) > 0);
    TEST_ASSERT_TRUE(strlen(theme->displayName()) > 0);

    theme->begin();

    /* ── All 9 FaceEvent values ──────────────────────────────────── */
    FaceEvent all_events[] = {
        FaceEvent::Idle,          FaceEvent::Recording,
        FaceEvent::VoiceActive,   FaceEvent::Silence,
        FaceEvent::Saving,        FaceEvent::Uploading,
        FaceEvent::UploadSuccess, FaceEvent::UploadError,
        FaceEvent::LowBattery,
    };

    float voice_levels[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    for (int e = 0; e < FACE_EVENT_COUNT; e++) {
        theme->setEvent(all_events[e]);

        /* Exercise update() at different voice levels */
        for (int v = 0; v < 5; v++) {
            theme->update(voice_levels[v], voice_levels[v] > 0.1f, 33);
        }

        /* Exercise draw() */
        stub_display_reset_counters();
        theme->draw();

        /* draw() must produce at least 1 display primitive call */
        TEST_ASSERT_GREATER_THAN(0, total_draw_calls());
    }

    /* ── Extended animation: run many update/draw cycles ─────────── */
    for (int i = 0; i < 50; i++) {
        float vl = (float)(i % 11) / 10.0f;  /* 0.0 .. 1.0 */
        theme->update(vl, vl > 0.05f, 16);
    }
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    delete theme;
}

/* ── Test: Owl ───────────────────────────────────────────────────────── */

extern "C" void test_face_theme_owl_all_events(void)
{
    FaceConfig cfg;
    exercise_theme(create_owl_face(cfg));
}

/* ── Test: Minimal ───────────────────────────────────────────────────── */

extern "C" void test_face_theme_minimal_all_events(void)
{
    FaceConfig cfg;
    exercise_theme(create_minimal_face(cfg));
}

extern "C" void test_face_theme_minimal_is_fallback(void)
{
    /* Verify id matches the fallback name */
    FaceConfig cfg;
    FacePlugin *theme = create_minimal_face(cfg);
    TEST_ASSERT_EQUAL_STRING("minimal", theme->id());
    delete theme;
}

/* ── Test: Robot ─────────────────────────────────────────────────────── */

extern "C" void test_face_theme_robot_all_events(void)
{
    FaceConfig cfg;
    exercise_theme(create_robot_face(cfg));
}

/* ── Test: Pixel ─────────────────────────────────────────────────────── */

extern "C" void test_face_theme_pixel_all_events(void)
{
    FaceConfig cfg;
    exercise_theme(create_pixel_face(cfg));
}

/* ── Test: Vector ────────────────────────────────────────────────────── */

extern "C" void test_face_theme_vector_all_events(void)
{
    FaceConfig cfg;
    exercise_theme(create_vector_face(cfg));
}

/* ── Test: Config values are respected ───────────────────────────────── */

extern "C" void test_face_theme_config_eye_range(void)
{
    /* Themes should accept custom eye ranges without crashing */
    FaceConfig cfg;
    cfg.eye_min_size = 1;
    cfg.eye_max_size = 40;

    FacePlugin *theme = create_owl_face(cfg);
    theme->begin();

    /* Max voice → should use eye_max_size path */
    theme->setEvent(FaceEvent::VoiceActive);
    theme->update(1.0f, true, 100);
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    /* Zero voice → should use eye_min_size path */
    theme->update(0.0f, false, 200);
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    delete theme;
}

extern "C" void test_face_theme_config_no_react(void)
{
    /* react_to_voice = false: eyes should stay at min regardless of voice */
    FaceConfig cfg;
    cfg.react_to_voice = false;
    cfg.eye_min_size = 8;

    FacePlugin *theme = create_minimal_face(cfg);
    theme->begin();

    theme->setEvent(FaceEvent::VoiceActive);
    theme->update(1.0f, true, 100);
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    delete theme;
}

extern "C" void test_face_theme_config_no_blink(void)
{
    /* blink = false: blink animation should be suppressed (no crash) */
    FaceConfig cfg;
    cfg.blink = false;

    FacePlugin *theme = create_robot_face(cfg);
    theme->begin();

    /* Run many update cycles (would trigger blink if enabled) */
    for (int i = 0; i < 100; i++) {
        theme->update(0.3f, false, 50);
    }
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    delete theme;
}

/* ── Test: begin() resets state ──────────────────────────────────────── */

extern "C" void test_face_theme_begin_resets_state(void)
{
    FaceConfig cfg;
    FacePlugin *theme = create_owl_face(cfg);

    /* Drive theme to a non-default state */
    theme->begin();
    theme->setEvent(FaceEvent::UploadError);
    theme->update(0.8f, true, 200);
    theme->draw();

    /* Re-begin → should be back to Idle */
    theme->begin();
    stub_display_reset_counters();
    theme->draw();
    TEST_ASSERT_GREATER_THAN(0, total_draw_calls());

    delete theme;
}

/* ── Test: all 5 themes have unique ids ──────────────────────────────── */

extern "C" void test_face_theme_unique_ids(void)
{
    FaceConfig cfg;
    FacePlugin *owl    = create_owl_face(cfg);
    FacePlugin *min    = create_minimal_face(cfg);
    FacePlugin *robot  = create_robot_face(cfg);
    FacePlugin *pixel  = create_pixel_face(cfg);
    FacePlugin *vector = create_vector_face(cfg);

    FacePlugin *themes[] = { owl, min, robot, pixel, vector };
    int count = sizeof(themes) / sizeof(themes[0]);
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            TEST_ASSERT_TRUE(strcmp(themes[i]->id(), themes[j]->id()) != 0);
        }
    }

    delete owl;
    delete min;
    delete robot;
    delete pixel;
    delete vector;
}
