---
name: new-face
description: Create a new face theme for the echo-pocket face engine (components/ui/face_engine). Use when the user wants to add a new face, character, or theme for the robot's display.
---

# Adding a face theme

A face theme is one `.cpp` file implementing `FacePlugin` (`components/ui/face_engine/face_plugin.hpp`). `vector_face.cpp` (Anki Vector-style blocky eyes) is the best reference — read it first for structure and animation timing style.

## The 9 states a face MUST react to

`FaceEvent` (`face_plugin.hpp`) — every theme must give a visually distinct
response to each, at least via `eyeColor()`/mouth shape:

| Event | Meaning | Typical treatment |
|---|---|---|
| `Idle` | home screen, nothing happening | resting/neutral eyes, calm blink |
| `Recording` | recording in progress | "active" eye color |
| `VoiceActive` | VAD detected speech above threshold | active color, often larger eyes (react to `voiceLevel`) |
| `Silence` | recording but no voice detected | same as Recording, or slightly duller |
| `Saving` | finalizing WAV file | active color, brief state |
| `Uploading` | network upload in progress | distinct look — vector_face narrows/"strains" the eyes |
| `UploadSuccess` | upload ok | happy — smile mouth, normal/rounded eyes |
| `UploadError` | upload failed, will retry | warn/error color, frown |
| `LowBattery` | battery ≤20%/≤10% | warn color, frown |

Use `face_event_label(event)` for the shared text label shown under the face (don't invent per-theme label text).

## Required shape (copy vector_face.cpp)

```cpp
#include "face_plugin.hpp"
#include "face_themes.h"
#include "display.h"

class MyFace : public FacePlugin {
public:
    MyFace(const FaceConfig &cfg) : cfg_(cfg), event_(FaceEvent::Idle) {}
    const char* id() const override          { return "myface"; }   // short, unique, matches [face].theme in recorder.ini
    const char* displayName() const override { return "My Face"; }  // shown in Face submenu
    void begin() override { /* reset animation state */ }
    void setEvent(FaceEvent event) override { event_ = event; }
    void update(float voiceLevel, bool voiceDetected, uint32_t deltaMs) override { /* advance animation by deltaMs */ }
    void draw() override { /* display_clear() then display_* calls */ }
private:
    FaceConfig cfg_;
    FaceEvent  event_;
};

FacePlugin *create_my_face(const FaceConfig &cfg) { return new MyFace(cfg); }
```

Rules enforced by the interface itself:
- No access to recorder/network/SD state — only `voiceLevel`, `voiceDetected`, `deltaMs`, and the current `FaceEvent`.
- No exceptions (ESP-IDF C++ builds them disabled).
- `FaceConfig` gives `eye_min_size`/`eye_max_size`/`react_to_voice`/`blink` — honor `react_to_voice` and `blink` if the design has eyes/blinking; themes without literal "eyes" (e.g. pixel-art) can ignore what doesn't apply.
- `draw()` must call at least one `display_*` primitive every time (tests assert this).

Available primitives — see `components/ui/include/display.h`: `display_clear`, `display_draw_text`, `display_fill_rect`, `display_fill_rounded_rect`, `display_draw_rect`, `display_fill_circle`, `display_draw_hline`, `display_draw_smile_arc`. No arbitrary line/polygon/bezier primitives exist — designs must be buildable from these.

## Wiring up a new theme (4 places)

1. **New file** `components/ui/face_engine/<name>_face.cpp` implementing the class above.
2. **Declare the factory** in `components/ui/face_engine/face_themes.h`:
   `FacePlugin *create_<name>_face(const FaceConfig &cfg);`
3. **Register** in `face_registry_register_defaults()` in `components/ui/face_engine/face_registry.cpp`:
   `face_registry_register(create_<name>_face(defaults));`
4. **Add to both build files** (new source file must be listed in both places or it won't compile):
   - `components/ui/CMakeLists.txt` — add `"face_engine/<name>_face.cpp"` to SRCS
   - `test_apps/logic_tests/main/CMakeLists.txt` — add `"../../../components/ui/face_engine/<name>_face.cpp"`

## Testing

Add a smoke test in `test_apps/logic_tests/main/test_face_themes.cpp` following the existing per-theme pattern (`test_face_theme_vector_all_events`), calling `exercise_theme(create_<name>_face(cfg))` — this drives all 9 events, several voice levels, and asserts `draw()` emits display calls. Then in `test_apps/logic_tests/main/test_main.c`: add `extern void test_face_theme_<name>_all_events(void);` next to the other `extern` declarations, and `RUN_TEST(test_face_theme_<name>_all_events);` next to the other `RUN_TEST` calls.

Build/run logic tests per `docs/plans` / README instructions for `test_apps/logic_tests`. Also update `AGENTS.md`'s face-engine section only if the built-in theme count/list is mentioned there (currently references "≥4 built-in themes" — no need to bump numbers per new face).
