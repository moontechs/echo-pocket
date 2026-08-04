/** @file test_face_registry.cpp
 * @brief Unit tests for the face engine registry (Task 9).
 *
 * Compiled into test_apps/logic_tests as a C++ source.  Test functions
 * use extern "C" linkage so test_main.c can call them via RUN_TEST.
 */

#include "unity.h"
#include "face_registry.h"
#include "face_plugin.hpp"
#include <string.h>
#include <stdio.h>

/* ── Mock FacePlugin for registry tests ──────────────────────────────── */

/** Records every FaceEvent dispatched through setEvent(). */
struct SpyFace : public FacePlugin {
    const char *id_;
    const char *display_name_;
    int         begin_count;
    int         update_count;
    int         draw_count;
    FaceEvent   last_event;
    int         event_counts[FACE_EVENT_COUNT]; /* indexed by enum value */
    float       last_voice_level;
    bool        last_voice_detected;
    uint32_t    last_delta_ms;

    SpyFace(const char *id, const char *display_name)
        : id_(id), display_name_(display_name),
          begin_count(0), update_count(0), draw_count(0),
          last_event(FaceEvent::Idle),
          last_voice_level(0.0f), last_voice_detected(false), last_delta_ms(0)
    {
        for (int i = 0; i < FACE_EVENT_COUNT; i++) event_counts[i] = 0;
    }

    const char* id() const override          { return id_; }
    const char* displayName() const override { return display_name_; }

    void begin() override                    { begin_count++; }

    void setEvent(FaceEvent e) override {
        last_event = e;
        int idx = static_cast<int>(e);
        if (idx >= 0 && idx < FACE_EVENT_COUNT) {
            event_counts[idx]++;
        }
    }

    void update(float vl, bool vd, uint32_t dm) override {
        update_count++;
        last_voice_level = vl;
        last_voice_detected = vd;
        last_delta_ms = dm;
    }

    void draw() override { draw_count++; }
};

/* ── Helper: spies are re-created fresh in setUp() each test ──────── */

static SpyFace *g_spy_minimal = NULL;
static SpyFace *g_spy_owl     = NULL;
static SpyFace *g_spy_robot   = NULL;

/* ── setUp / tearDown ────────────────────────────────────────────────── */

extern "C" void setUp(void)
{
    /* Re-create spies fresh each test */
    delete g_spy_minimal;
    delete g_spy_owl;
    delete g_spy_robot;

    g_spy_minimal = new SpyFace("minimal", "Minimal");
    g_spy_owl     = new SpyFace("owl",     "Owl");
    g_spy_robot   = new SpyFace("robot",   "Robot");

    face_registry_init();
}

extern "C" void tearDown(void)
{
    /* Clean up at end of test group */
}

/* ── Test: empty registry returns NULL ───────────────────────────────── */

extern "C" void test_face_registry_empty_count(void)
{
    TEST_ASSERT_EQUAL(0, face_registry_count());
}

extern "C" void test_face_registry_empty_find(void)
{
    TEST_ASSERT_NULL(face_registry_find("minimal"));
}

extern "C" void test_face_registry_empty_active_is_null(void)
{
    TEST_ASSERT_NULL(face_registry_get_active());
}

/* ── Test: register and count ────────────────────────────────────────── */

extern "C" void test_face_registry_register_increases_count(void)
{
    TEST_ASSERT_EQUAL(1, face_registry_register(g_spy_minimal));
    TEST_ASSERT_EQUAL(1, face_registry_count());

    TEST_ASSERT_EQUAL(2, face_registry_register(g_spy_owl));
    TEST_ASSERT_EQUAL(2, face_registry_count());
}

extern "C" void test_face_registry_register_null_noop(void)
{
    TEST_ASSERT_EQUAL(0, face_registry_register(NULL));
    TEST_ASSERT_EQUAL(0, face_registry_count());
}

/* ── Test: find by id ────────────────────────────────────────────────── */

extern "C" void test_face_registry_find_by_id(void)
{
    face_registry_register(g_spy_minimal);
    face_registry_register(g_spy_owl);

    FacePlugin *found = face_registry_find("owl");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("owl", found->id());

    found = face_registry_find("minimal");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("minimal", found->id());
}

extern "C" void test_face_registry_find_missing_returns_null(void)
{
    face_registry_register(g_spy_minimal);
    TEST_ASSERT_NULL(face_registry_find("pixel"));
    TEST_ASSERT_NULL(face_registry_find(""));
    TEST_ASSERT_NULL(face_registry_find(NULL));
}

/* ── Test: unknown theme falls back to minimal ───────────────────────── */

extern "C" void test_face_registry_fallback_to_minimal(void)
{
    face_registry_register(g_spy_minimal);
    face_registry_register(g_spy_owl);

    /* Request unknown theme */
    face_registry_begin("nonexistent");

    /* Should have fallen back to minimal */
    FacePlugin *active = face_registry_get_active();
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_STRING("minimal", active->id());
    TEST_ASSERT_EQUAL(1, g_spy_minimal->begin_count);
    TEST_ASSERT_EQUAL(0, g_spy_owl->begin_count);
}

extern "C" void test_face_registry_fallback_calls_begin_on_fallback(void)
{
    face_registry_register(g_spy_minimal);
    face_registry_begin("unknown");
    TEST_ASSERT_EQUAL(1, g_spy_minimal->begin_count);
}

/* ── Test: direct resolution of known theme ──────────────────────────── */

extern "C" void test_face_registry_begin_known(void)
{
    face_registry_register(g_spy_minimal);
    face_registry_register(g_spy_owl);

    face_registry_begin("owl");

    FacePlugin *active = face_registry_get_active();
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_STRING("owl", active->id());
    TEST_ASSERT_EQUAL(1, g_spy_owl->begin_count);
    TEST_ASSERT_EQUAL(0, g_spy_minimal->begin_count);
}

/* ── Test: active is NULL when no themes registered ──────────────────── */

extern "C" void test_face_registry_begin_empty_registry(void)
{
    face_registry_begin("minimal");
    TEST_ASSERT_NULL(face_registry_get_active());
}

/* ── Test: every FaceEvent value dispatches without crash ────────────── */

extern "C" void test_face_registry_all_events_dispatch(void)
{
    face_registry_register(g_spy_minimal);
    face_registry_register(g_spy_owl);
    face_registry_begin("owl");

    FacePlugin *active = face_registry_get_active();
    TEST_ASSERT_NOT_NULL(active);

    /* Dispatch every FaceEvent value */
    FaceEvent all_events[] = {
        FaceEvent::Idle,
        FaceEvent::Recording,
        FaceEvent::VoiceActive,
        FaceEvent::Silence,
        FaceEvent::Saving,
        FaceEvent::Uploading,
        FaceEvent::UploadSuccess,
        FaceEvent::UploadError,
        FaceEvent::LowBattery,
    };

    for (int i = 0; i < FACE_EVENT_COUNT; i++) {
        active->setEvent(all_events[i]);
    }

    /* Verify owl received every event exactly once */
    for (int i = 0; i < FACE_EVENT_COUNT; i++) {
        TEST_ASSERT_EQUAL(1, g_spy_owl->event_counts[i]);
    }
}

/* ── Test: frame-rate cap — initial call always passes ───────────────── */

extern "C" void test_face_registry_fps_initial_call(void)
{
    face_registry_set_frame_interval_ms(50); /* 20 fps */
    /* First call at any timestamp should succeed (s_last_draw_ms starts at 0) */
    TEST_ASSERT_TRUE(face_registry_should_update(100));
}

/* ── Test: frame-rate cap — respects interval ────────────────────────── */

extern "C" void test_face_registry_fps_within_interval(void)
{
    face_registry_set_frame_interval_ms(100); /* 10 fps */

    TEST_ASSERT_TRUE(face_registry_should_update(1000)); /* first frame */
    /* Immediately after — same timestamp */
    TEST_ASSERT_FALSE(face_registry_should_update(1000));
    /* Within interval */
    TEST_ASSERT_FALSE(face_registry_should_update(1050));
    /* After interval */
    TEST_ASSERT_TRUE(face_registry_should_update(1100));
}

extern "C" void test_face_registry_fps_zero_uncapped(void)
{
    face_registry_set_frame_interval_ms(0);
    TEST_ASSERT_TRUE(face_registry_should_update(0));
    TEST_ASSERT_TRUE(face_registry_should_update(0));
    TEST_ASSERT_TRUE(face_registry_should_update(1000));
}

/* ── Test: frame-rate cap — counter wraparound ───────────────────────── */

extern "C" void test_face_registry_fps_wraparound(void)
{
    face_registry_set_frame_interval_ms(50);

    /* Render at a large timestamp */
    TEST_ASSERT_TRUE(face_registry_should_update(0xFFFFFF00));
    /* Now wraparound: now_ms < last */
    TEST_ASSERT_TRUE(face_registry_should_update(0x00000010));
}

/* ── Test: update() and draw() routing through active plugin ──────────── */

extern "C" void test_face_registry_active_update_routing(void)
{
    face_registry_register(g_spy_robot);
    face_registry_begin("robot");

    FacePlugin *active = face_registry_get_active();
    TEST_ASSERT_NOT_NULL(active);
    active->update(0.75f, true, 33);

    TEST_ASSERT_EQUAL(1, g_spy_robot->update_count);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, g_spy_robot->last_voice_level);
    TEST_ASSERT_TRUE(g_spy_robot->last_voice_detected);
    TEST_ASSERT_EQUAL(33, g_spy_robot->last_delta_ms);
}

extern "C" void test_face_registry_active_draw_routing(void)
{
    face_registry_register(g_spy_robot);
    face_registry_begin("robot");

    FacePlugin *active = face_registry_get_active();
    active->draw();
    TEST_ASSERT_EQUAL(1, g_spy_robot->draw_count);
}
