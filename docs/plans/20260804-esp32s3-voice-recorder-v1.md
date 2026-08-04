# ESP32-S3 Voice Recorder v1.0

## Overview

Autonomous voice recorder firmware for the Waveshare ESP32-S3-LCD-1.54 (240×240 LCD, no
touch, dual mic + ES7210, microSD, 3 buttons, Wi-Fi/BLE). Records to WAV on microSD with
ESP-SR noise suppression, shows a voice-reactive animated face (4 swappable built-in themes),
uploads recordings to Telegram via a crash-safe persistent queue, and is fully configured from
a plain-text INI file on the SD card. Full spec: `AGENTS.md`.

This plan covers the complete v1.0 roadmap (AGENTS.md §14, Stages 1–6) in one sequence, ordered
so each stage is runnable and testable on real hardware before the next begins.

Revised after `planning:plan-review`: fixed the audio pipeline topology (ring buffer sits
between capture and AFE, not after it), added a real test harness instead of a nonexistent
`idf.py test`, added the offline time source, config write-back, and the config keys that were
parsed but never wired to a consumer, fixed component/CMake layout issues, and split the
overloaded UI task. Over-engineered test mocking (C++ exceptions, `esp_wifi` mocks, an
app_main call-order test) was dropped in favor of the on-device checks that actually validate
those paths.

Revised after `critic`: split Task 1 so hardware-independent scaffolding no longer blocks on
vendor-docs pinout research; added a RAM/PSRAM budget sanity check before ESP-SR lands; added a
Telegram file-size-limit check to Task 16; documented the FreeRTOS task priority order in Task 6
so "capture never blocks" is enforced, not just asserted; and recorded two accepted risks
(plaintext credentials on removable SD, no SD wear-leveling mitigation) that were previously
unaddressed.

Revised after second `planning:plan-review`: moved `device_events.h` out of `components/ui` into
`components/board` to break a `ui ↔ recorder` circular component dependency; moved the
octal-vs-quad PSRAM decision into Task 1's hardware-dependent block (it's a module-revision fact,
not something safe to assume); extended the config-key consumed/inert audit (Task 5/Task 20) to
`[device]` and `[telegram]`, added `[device].timezone` application via `setenv/tzset` to Task 14,
and named `[device].name` in Task 16's caption bullet; fixed Task 6's task-priority cross-
reference to include Tasks 3/7/8; relocated the SD-wear accepted risk next to the credentials
risk in Technical Details and corrected it to name `index.json` rewrites (not WAV header
patching) as the actual write-amplification source; narrowed Task 1's pin-constants test to
within-group distinctness only (dropped the wrong "non-zero" assertion — GPIO0 is a legitimate
pin); added the same forward-dependency note to Task 11 that Task 13 already had for Task 15; and
gave Tasks 16/18 concrete numbers (byte threshold for "large" upload, a runtime guard instead of
just a comment for the file-size check).

## Context (from discovery)

- Repo is currently empty except `AGENTS.md` / `CLAUDE.md` (spec docs) — greenfield, no code.
- Not a git repo yet.
- Target framework: ESP-IDF (per AGENTS.md §13).
- Vendor reference: `github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54` ships ESP-IDF examples
  covering both the touch and non-touch (`ESP32-S3-LCD-1.54`) board variants — exact GPIO
  pin numbers and VBAT/charger wiring are **not** available from the public docs pages
  (403/no pin table), so Task 1 starts by extracting the real pinout and battery-monitoring
  answer from that vendor example/schematic rather than guessing.
- No existing test framework. ESP-IDF unit tests run as their own flashable app, not via a
  generic `idf.py test` command — Task 1 creates a dedicated `test_apps/logic_tests/` ESP-IDF
  project that links the pure-logic components and runs their Unity test cases on-target
  (`idf.py -C test_apps/logic_tests build flash monitor`); that is the concrete command every
  later "run tests" checkbox refers to. Hardware-dependent behavior (I2S, LCD, SD, buttons,
  real Wi-Fi/Telegram) is verified on-device per stage and tracked as Post-Completion manual
  checks, not unit tests.

## Development Approach

- **Testing approach**: Regular (code first, then tests) — matches embedded/hardware bring-up
  work where the first pass is "does it run on the board," not spec-first.
- Complete each task fully, verify on real hardware where applicable, before moving on.
- Every task with non-trivial logic (parsers, state machines, math) gets a Unity test in
  `test_apps/logic_tests`, run with the exact command from Task 1; hardware-only tasks
  (peripheral bring-up) get a documented manual on-device check instead — called out explicitly
  per task. Don't reach for mocking frameworks (CMock, fakes) for logic that's cheaper to
  factor into a pure function and test directly — see Tasks 14 and 19 for the pattern.
- Audio capture must never block on SD, display, or network (AGENTS.md — architecture
  requirement): capture writes into the PSRAM ring buffer and nothing else; the AFE and the SD
  writer are both downstream consumers, never upstream of capture. Don't violate this ordering
  in later tasks.
- Update this plan's checkboxes and add ➕/⚠️ markers as work proceeds.

## Testing Strategy

- **Unit tests (Unity, `test_apps/logic_tests`)**: config INI parser + write-back, upload-queue
  state machine, WAV header read/patch logic, offline filename fallback, battery
  percent-from-voltage lookup, Telegram caption formatting, face event→theme dispatch (registry
  fallback to `minimal`), menu navigation state machine, ring buffer wraparound/overflow,
  Wi-Fi network-selection ordering (as a pure function, no `esp_wifi` mock), upload drain
  attempts→`failed` transition. Must pass before the next task starts.
- **On-device manual checks**: LCD draw, button debounce, SD mount, I2S/mic capture, ESP-SR
  output quality, Wi-Fi reconnect, real Telegram delivery, battery reading vs multimeter,
  power-loss/reboot recovery, SPI-bus contention between LCD and SD during a recording. Called
  out per-task and consolidated in Post-Completion.
- No e2e/UI browser tests — not applicable to embedded firmware.

## Progress Tracking

- Mark completed items `[x]` immediately when done.
- Add newly discovered tasks with ➕ prefix; blockers with ⚠️ prefix.
- Keep this file in sync with actual implementation; update task list if scope changes.

## Solution Overview

Single ESP-IDF project, component-per-concern layout (`board`, `audio`, `recorder`, `ui`,
`storage`, `network`, `telegram`), FreeRTOS tasks communicating over queues/ring buffers so the
audio path is never blocked by UI/SD/network:

```
I2S capture (2ch) → PSRAM ring buffer → audio_process_task (ESP-SR AFE) → writer queue → sd_writer_task
```

Device-state changes (recording started/stopped/saving, upload progress, low battery) publish
onto one `esp_event` loop; `ui_task` is the sole subscriber that turns those into `FaceEvent`s
for the active theme — so recorder/upload/battery code never talks to the UI directly. Build
bottom-up: board bring-up → raw WAV recording → noise suppression → UI/face engine → Telegram →
durable upload queue — each stage matches AGENTS.md §14 and is independently flashable/testable.

## Technical Details

- Audio: 2 mic → ES7210 → I2S (2ch, s16, 16kHz) → PSRAM ring buffer → ESP-SR AFE
  (NS/VAD/AGC, mono out) → mono PCM → WAV on SD. Capture never waits on the AFE, the writer, or
  the ring buffer being full (overflow is counted and surfaced, never silently dropped without
  a trace).
- Storage layout: everything lives under one app root on SD, `/echo-pocket/`, so the card can
  hold other files/apps without collision: `/echo-pocket/config/recorder.ini`,
  `/echo-pocket/rec/REC_YYYYMMDD_HHMMSS_NNN.wav`, `/echo-pocket/queue/index.json`,
  `/echo-pocket/logs/` (reserved; no writer in v1.0 — do not build a log sink that isn't
  requested by the spec).
- Time source: ESP32-S3 has no battery-backed RTC. Wall-clock time is only known after
  Wi-Fi + SNTP; recordings taken before that (or fully offline) use a monotonic boot-relative
  counter fallback in the filename/id so the device works fully offline per AGENTS.md §3.4/§11.
- Queue states: `recording → pending → uploading → sent | failed`; `uploading` reverts to
  `pending` on boot (crash recovery); `failed` is terminal after N attempts, cleared back to
  `pending` only by manual "Send All".
- Face engine: `FacePlugin` C++ interface (see AGENTS.md), compiled-in registry, 4 themes,
  unknown theme in config → falls back to `minimal`, active theme persisted back to
  `recorder.ini`, frame rate capped so it never competes with the audio path for CPU time.
- Config: plain-text INI on SD, invalid/missing config degrades gracefully (SD recording keeps
  working, Telegram/Wi-Fi disable, screen shows a clear error) — never a firmware crash. Every
  key in AGENTS.md's `recorder.ini` example is either wired to a real consumer or explicitly
  documented as parsed-but-inert for v1.0 (see Task 5).
  - **Accepted risk**: the Wi-Fi password(s) and Telegram bot token live in this plaintext INI
    on a removable SD card — anyone who pulls the card gets both. No encryption/secret-store is
    planned for v1.0; this is a known tradeoff of the "plain-text config on SD" design, not an
    oversight. If this needs closing later, it's a v-next item, not a v1.0 blocker.
  - **Accepted risk**: no SD wear-leveling/endurance mitigation planned for v1.0. The actual
    write-amplification source is `/echo-pocket/queue/index.json` (Task 15), rewritten atomically
    on every queue state transition — WAV header patching (Task 7) only happens once per file and
    isn't the concern. Use a high-endurance card; card wear-out over long-term continuous use is
    a known v1.0 tradeoff, not something firmware mitigates.

## What Goes Where

- **Implementation Steps**: all firmware code, on-device bring-up, and the unit tests listed above.
- **Post-Completion**: physical hardware verification (long-duration recording soak test, real
  Telegram delivery, battery accuracy vs multimeter, power-loss recovery drill, SPI bus
  contention check) — these require the physical board and can't be scripted here.

## Implementation Steps

### Task 1: Confirm hardware, bootstrap ESP-IDF project, stand up the test harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `main/CMakeLists.txt`
- Create: `main/app_main.c`
- Create: `sdkconfig.defaults`
- Create: `components/board/CMakeLists.txt`
- Create: `components/board/include/board.h`
- Create: `components/board/board_pins.c`
- Create: `test_apps/logic_tests/CMakeLists.txt`
- Create: `test_apps/logic_tests/main/CMakeLists.txt`
- Create: `test_apps/logic_tests/main/test_main.c`
- Create: `.gitignore`

Hardware-independent scaffolding (does not depend on vendor docs — do this first, doesn't block
on the pinout research below; note this half alone doesn't unblock Task 2, which still needs the
real pinout from the research half):
- [ ] scaffold the ESP-IDF project targeting `esp32s3`, enable PSRAM in `sdkconfig.defaults`
      (leave octal-vs-quad mode as the module default for now — the actual mode is set in the
      hardware-dependent block below once the module revision is confirmed)
- [ ] create `test_apps/logic_tests`: a second ESP-IDF project that adds the main project's
      `components/` via `EXTRA_COMPONENT_DIRS` and runs Unity over serial; this is the concrete
      target every later "run tests" checkbox means — command:
      `idf.py -C test_apps/logic_tests build flash monitor`
- [ ] `idf.py build` (main project) succeeds and produces a flashable binary
- [ ] rough RAM/PSRAM budget pass before later tasks lock in sizes: note (as a comment in
      `sdkconfig.defaults`) the expected PSRAM consumers — audio ring buffer (Task 6), ESP-SR AFE
      model (Task 8, check the `esp-sr` component's documented footprint for the smallest
      NS/VAD/AGC model set), LCD framebuffer (Task 2), Wi-Fi/BLE stack heap — and confirm they
      plausibly fit the board's PSRAM/internal RAM split; this is a sanity check, not a hard
      allocation, but catches an "ESP-SR doesn't fit next to Wi-Fi" surprise now instead of at
      Task 8/14

Hardware-dependent pinout/battery research (blocked on vendor docs — if the vendor example is
unreachable, the scaffolding above still lands and this can be retried without blocking it):
- [ ] clone/inspect `waveshareteam/ESP32-S3-Touch-LCD-1.54` ESP-IDF example (scratch dir, not
      committed) to extract the real GPIO map for LCD SPI (MOSI/SCLK/CS/DC/RST/BL), the 3
      buttons, SD SPI/SDMMC pins, I2S pins (BCLK/WS/DIN/DOUT/MCLK), I2C pins to ES7210 — record
      as constants in `board_pins.c`, flag anything unconfirmed with `// TODO(hw-verify)`
  - [ ] in the same pass, confirm the module's PSRAM revision (octal vs quad, e.g. ESP32-S3R8 vs
      R2/R4 from the module marking or vendor `sdkconfig.defaults`) and set the correct mode in
      `sdkconfig.defaults` — getting this wrong from the wrong assumption is a boot hang, not a
      compile error
  - [ ] in the same pass, determine the battery-monitoring answer needed by Task 18: is VBAT
      exposed via a resistor divider to an ADC pin, is charger status readable, does any of
      those pins conflict with another peripheral — record the verdict (including "not
      exposed") as a constant/comment in `board.h`, this gates Task 18's branch
- [ ] create `components/board` with pin constants and a `board_init()` stub
- [ ] write a Unity test in `logic_tests` asserting pin constants are unique *within each
      function group* (buttons distinct from each other, I2S pins distinct from each other) —
      do NOT assert global uniqueness across groups (LCD and SD sharing an SPI bus is legitimate
      on this board) and do NOT assert non-zero (GPIO0 is a legitimate, if strapping, pin)
- [ ] run `idf.py -C test_apps/logic_tests build flash monitor` — must pass before task 2

### Task 2: LCD bring-up (ST7789 SPI, 240×240)

**Files:**
- Create: `components/ui/CMakeLists.txt`
- Create: `components/ui/include/display.h`
- Create: `components/ui/display.c`

- [ ] init ST7789 over SPI using pins from Task 1 (`esp_lcd` component from the IDF component
      registry — don't hand-roll a driver)
- [ ] draw a full-screen test pattern + text on boot to confirm orientation/colors
- [ ] expose a minimal `display_clear()/display_draw_text()` API for later UI code
- [ ] manual on-device check: pattern renders correctly, right-side-up, correct colors (no
      unit test — hardware framebuffer output)
- [ ] run `logic_tests` — must still pass before task 3

### Task 3: Button driver (3 buttons, short-press only)

**Files:**
- Create: `components/board/buttons.c`
- Create: `components/board/include/buttons.h`
- Create: `components/board/test/test_buttons.c`
- Modify: `components/board/CMakeLists.txt`

- [ ] GPIO input + debounce (interrupt or polling task) for left/center/right, emits a
      `ButtonEvent{LEFT,CENTER,RIGHT}` onto one FreeRTOS queue on short-press release
- [ ] no long-press/double-press logic in v1.0 (explicitly out of scope per spec §5)
- [ ] write Unity test for the debounce state machine using simulated GPIO transitions (pure
      logic, no real GPIO needed)
- [ ] manual on-device check: each button reliably produces exactly one event per press
- [ ] run `logic_tests` — must pass before task 4

### Task 4: SD card mount and directory bootstrap

**Files:**
- Create: `components/storage/CMakeLists.txt`
- Create: `components/storage/include/sd_storage.h`
- Create: `components/storage/sd_storage.c`
- Create: `components/storage/test/test_sd_storage.c`

- [ ] mount SD via SPI/SDMMC (per Task 1 pinout) using `esp_vfs_fat_sdcard_mount`; note whether
      it shares the SPI bus with the LCD (Task 2) — if so, the bus must be arbitrated (shared
      SPI host + per-device locking) so a display redraw can't corrupt an in-flight SD write
- [ ] on mount, ensure `/echo-pocket/config`, `/echo-pocket/rec`, `/echo-pocket/queue`,
      `/echo-pocket/logs` exist (create the whole tree if missing) — everything the app touches
      lives under the `/echo-pocket/` root so the card can hold other data without collision;
      `/echo-pocket/logs` is created for layout completeness per AGENTS.md §Recording on SD;
      nothing writes
      to it in v1.0, don't build a logging sink that wasn't asked for
- [ ] surface a clear "SD not detected/mount failed" error state instead of crashing
- [ ] write Unity test for the directory-bootstrap logic against a temp FAT path (or documented
      on-device-only check if the FAT stack can't run under `logic_tests`)
- [ ] manual on-device check: fresh/blank SD card boots and gets the directory structure; if
      bus-shared with the LCD, confirm no corruption during simultaneous draw+write
- [ ] run `logic_tests` — must pass before task 5

### Task 5: `recorder.ini` config parser + write-back

**Files:**
- Create: `components/storage/config.c`
- Create: `components/storage/include/config.h`
- Create: `components/storage/test/test_config.c`
- Modify: `components/storage/CMakeLists.txt`

- [ ] parse INI sections from AGENTS.md §Config (`[device]`, `[wifi_N]`, `[telegram]`,
      `[recorder]`, `[face]`) into a `RecorderConfig` struct
- [ ] missing/invalid file or malformed keys → fall back to safe defaults: SD recording stays
      enabled, Telegram disabled, Wi-Fi may be absent, clear on-screen error — never abort boot
- [ ] `config_save(cfg)`: atomic write-back (write temp file + rename) so later tasks (theme
      persistence in Task 12) have a safe place to land — the face registry itself still
      resolves an unknown theme string to `minimal` (that's Task 9, not here)
- [ ] document explicitly, in a comment block at the top of `config.h`, which keys across ALL
      sections are consumed where in this plan (not just `[recorder]`): `[recorder].auto_upload`
      →Task 17, `[recorder].delete_after_upload`→Task 17,
      `[recorder].noise_suppression`/`voice_detection`→Task 8, `[recorder].sample_rate`→Task 6
      (fixed at 16000 for v1.0, key is parsed but not variable), `[device].timezone`→Task 14
      (applied via `setenv("TZ", ...)/tzset()` after SNTP sync), `[device].name`→Task 16 (used in
      the upload caption's `Device:` line), `[telegram].active_channel`/`channel_N_id`/
      `channel_N_name`→Task 16/17 (`send_to_all` fan-out target selection) — so nothing is
      silently dropped by Task 20's review
- [ ] write Unity tests: valid config parses correctly; missing file falls back safely;
      malformed lines/sections are skipped without crashing; multiple `wifi_N` entries parsed;
      `config_save` round-trips and survives a simulated interrupted write (temp file present,
      real file untouched)
- [ ] run `logic_tests` — must pass before task 6

### Task 6: Audio capture path — I2S (2-channel) + ES7210 + PSRAM ring buffer

**Files:**
- Create: `components/audio/CMakeLists.txt`
- Create: `components/audio/include/audio_capture.h`
- Create: `components/audio/audio_capture.c`
- Create: `components/audio/test/test_ring_buffer.c`

- [ ] init ES7210 over I2C using `esp_codec_dev` (IDF managed component with an ES7210 driver —
      don't hand-roll codec register init) configured for 2-mic input; init I2S RX at
      16kHz/**2-channel**/16-bit (matches what the ESP-SR AFE in Task 8 expects; do not
      downmix to mono here)
- [ ] `audio_capture_task`: reads I2S frames, writes into a PSRAM ring buffer sized for several
      seconds of 2-channel audio — capture must never block on a full/contended buffer; on
      overflow, drop the oldest frame but increment and log an overflow counter (never silently
      lose audio without a trace — AGENTS.md's "no dropouts" criterion needs this visible)
- [ ] assign this task a high FreeRTOS priority (above UI/network/upload) and a stack size
      sized from actual measured usage, not a guess; document the full task priority order
      (capture > AFE (Task 8)/writer (Task 7) > UI (Tasks 3, 11) > network/upload (Tasks 14, 17))
      in `audio_capture.h` since this is what makes "capture never blocks" actually true rather
      than just asserted in prose — later tasks creating their own FreeRTOS tasks must stay below
      capture's priority
- [ ] expose `audio_capture_read(buf, len)` for downstream consumers — at this point (before
      Task 8 lands the AFE) also add a trivial 2ch→mono average-downmix helper so raw WAV
      recording (Task 7) has something to write; Task 8 replaces this call site with the AFE
      output, it does not change the ring buffer's shape
- [ ] write Unity test for the ring buffer itself (wraparound, overflow counting,
      concurrent read/write) — pure logic, runs under `logic_tests` without real I2S
- [ ] manual on-device check: capture task keeps up in real time over a multi-minute run,
      overflow counter stays at 0
- [ ] run `logic_tests` — must pass before task 7

### Task 7: WAV writer, record start/stop lifecycle, offline time source

**Files:**
- Create: `components/recorder/CMakeLists.txt`
- Create: `components/recorder/include/recorder.h`
- Create: `components/recorder/recorder.c`
- Create: `components/recorder/wav_writer.c`
- Create: `components/recorder/rec_id.c`
- Create: `components/recorder/test/test_wav_writer.c`
- Create: `components/recorder/test/test_rec_id.c`

- [ ] `sd_writer_task` consumes downmixed PCM (Task 6's helper for now, Task 8's AFE output
      later — same call site, no rewrite) and writes WAV (PCM s16 mono 16kHz) with a
      placeholder header; on stop, patches the header (RIFF/data sizes) and `fsync`s, in that
      exact order per AGENTS.md §7.1
- [ ] `rec_id.c`: builds `REC_<timestamp>_<NNN>` where `<timestamp>` uses real wall-clock time
      if SNTP has synced (flag set by Task 14), otherwise a monotonic boot-relative counter
      (e.g. `BOOT_<uptime_s>`) so recording works correctly fully offline per AGENTS.md §3.4 —
      never blocks or fails because time isn't known yet
- [ ] `ui_task` is the only `ButtonEvent` consumer going forward (established in Task 11); for
      now, wire center-button `RECORDING` toggle directly in `recorder.c` as a temporary
      subscriber — call this out with a `// TODO(task-11): move to ui_task` comment so Task 11
      knows to remove it, not add a second consumer
- [ ] auto-split into a new WAV file at ~18–20 minutes (AGENTS.md §7.2), reusing the same
      finalize-then-reopen path as a normal stop (also reused later by Task 18's low-battery
      safe-stop)
- [ ] write Unity tests for WAV header math (correct sizes for known sample counts), the
      start/split/stop state transitions, and `rec_id` generation in both the synced and
      offline-fallback cases
- [ ] manual on-device check: 10-minute continuous recording has no audible dropouts, header is
      valid (plays in a standard WAV player)
- [ ] run `logic_tests` — must pass before task 8

### Task 8: ESP-SR noise suppression, VAD, AGC — insert into the existing pipeline

**Files:**
- Modify: `components/audio/audio_capture.c`
- Create: `components/audio/audio_process.c`
- Create: `components/audio/include/audio_process.h`
- Create: `components/audio/test/test_process_pipeline.c`
- Modify: `components/audio/CMakeLists.txt`
- Modify: `components/recorder/recorder.c`

- [ ] add ESP-SR (`esp-sr` managed component) AFE **downstream of the ring buffer**:
      `audio_process_task` reads 2-channel frames from the ring buffer (Task 6), runs
      NS + VAD + moderate AGC, and produces mono PCM — no WakeNet, no command recognition, no
      playback, no AEC (AGENTS.md §6.2). Capture (Task 6) is unaffected: it still only ever
      writes into the ring buffer
- [ ] gate NS/VAD/AGC individually on `[recorder].noise_suppression` / `voice_detection` from
      config (Task 5) so those keys are actually consumed, not just parsed
- [ ] replace the Task 6 downmix call site in `recorder.c` with `audio_process_task`'s mono
      output; also emit a rolling voice-level float alongside it (consumed by UI in Task 11)
- [ ] write Unity test for the voice-level computation/smoothing logic in isolation (feed
      synthetic PCM samples, assert expected level ranges) — the AFE model itself isn't unit
      tested, only the glue code around it
- [ ] manual on-device check: record raw vs AFE-cleaned side by side, confirm intelligibility
      improves without artifacts; confirm capture's ring-buffer overflow counter (Task 6) is
      still 0 under AFE CPU load — this is the check that the topology fix actually holds
- [ ] run `logic_tests` — must pass before task 9

### Task 9: Face engine core (plugin interface, registry, device-event bus)

**Files:**
- Create: `components/ui/face_engine/face_plugin.hpp`
- Create: `components/ui/face_engine/face_registry.cpp`
- Create: `components/ui/face_engine/include/face_registry.h`
- Create: `components/ui/face_engine/test/test_face_registry.cpp`
- Create: `components/board/include/device_events.h`
- Modify: `components/ui/CMakeLists.txt`

- [ ] define `FaceEvent` enum and `FacePlugin` abstract class exactly as specified in
      AGENTS.md; `face_engine` is a source subdirectory of the existing `ui` component (add its
      sources to `components/ui/CMakeLists.txt`) — it is **not** its own component, avoiding an
      unregistered nested-component directory
- [ ] `face_registry`: holds compiled-in theme instances, resolves active theme by id from
      config, **unknown id falls back to `minimal`**
- [ ] a theme's `update()`/`draw()` only ever receives `voiceLevel`/`voiceDetected`/`deltaMs`
      and `FaceEvent` — no access to recorder/network/SD state, enforced by the interface shape
      itself (no exception-handling needed: ESP-IDF C++ builds run with exceptions disabled by
      default and enabling them just to catch a theme bug is disproportionate to what the spec
      requires — isolation comes from the interface never handing out live handles)
- [ ] `device_events.h`: one `esp_event` base (`RECORDER_EVENTS`) carrying the device-state
      transitions later tasks publish (recording started/stopped/saving, upload
      progress/success/error, low battery) — `ui_task` (Task 11) is the sole subscriber that
      maps these to `FaceEvent::setEvent()` calls. Lives in `components/board/include/`, not
      `components/ui/`: publishers are `recorder` (Tasks 7/15), `network`/`upload_task`
      (Task 17), and `board/battery` (Task 18), while `ui_task` (Task 11) also calls
      `recorder_start()/recorder_stop()` directly — putting the shared event header in `ui`
      would make `ui` and `recorder` depend on each other's headers, an ESP-IDF component
      `REQUIRES` cycle. `board` has no dependency on either, so it's the neutral home
- [ ] frame-rate cap (from `[face].animation_fps`) enforced in the registry/UI task, not per-theme
- [ ] write Unity tests: unknown theme id resolves to `minimal`; every `FaceEvent` value maps to
      a defined registry dispatch path (exhaustive switch, no default-drop)
- [ ] run `logic_tests` — must pass before task 10

### Task 10: Built-in face themes — Owl, Minimal, Robot, Pixel

**Files:**
- Create: `components/ui/face_engine/owl_face.cpp`
- Create: `components/ui/face_engine/minimal_face.cpp`
- Create: `components/ui/face_engine/robot_face.cpp`
- Create: `components/ui/face_engine/pixel_face.cpp`
- Modify: `components/ui/face_engine/face_registry.cpp`
- Modify: `components/ui/CMakeLists.txt`

- [ ] implement all 4 themes against `FacePlugin`, each reacting to every `FaceEvent`
      (Idle/Recording/VoiceActive/Silence/Saving/Uploading/UploadSuccess/UploadError/LowBattery)
      per the eye-size/blink/glance/smile behavior in AGENTS.md §4.3
- [ ] eye size interpolates between `[face].eye_min_size`/`eye_max_size` driven by
      post-noise-suppression `voiceLevel` (Task 8), not raw mic level; blink behavior gated on
      `[face].blink`, level reactivity gated on `[face].react_to_voice` — both config keys
      wired to a real consumer here
- [ ] register all 4 themes in the registry
- [ ] write a table-driven Unity test asserting every theme handles every `FaceEvent` without
      crashing and produces a draw call (smoke test, not pixel-perfect rendering)
- [ ] manual on-device check: visually confirm each theme's reaction to real speech and silence
- [ ] run `logic_tests` — must pass before task 11

### Task 11: UI task, home/recording screens, button ownership handoff

**Files:**
- Create: `components/ui/include/ui_task.h`
- Create: `components/ui/ui_task.c`
- Create: `components/ui/screens/home_screen.c`
- Create: `components/ui/screens/recording_screen.c`
- Create: `components/ui/test/test_ui_button_routing.c`
- Modify: `components/recorder/recorder.c`
- Modify: `components/ui/CMakeLists.txt`

- [ ] `ui_task`: becomes the **sole** consumer of the `ButtonEvent` queue (Task 3) — remove
      Task 7's temporary direct subscription in `recorder.c` and call `recorder_start()` /
      `recorder_stop()` from here instead, so only one task ever reads that queue
- [ ] subscribe to `RECORDER_EVENTS` (Task 9) and drive the active face theme's `setEvent()`
      accordingly; drive the active theme's `update()` from the voice-level stream (Task 8) and
      the display (Task 2)
- [ ] Home screen: face, ready status, Wi-Fi, SD, pending-upload count, battery (placeholder
      until Task 18 wires the real value)
- [ ] Recording screen: face, REC indicator, timer only (no level meter by default); shows
      "Saved" only after the WAV is fsync'd and the record is enqueued (Task 15, which lands
      later — until then, treat fsync+close alone as "Saved" and note the dependency, same
      pattern as Task 13's stubbed Unsent list), not on stop alone — subscribe to the
      `Saving`/queue-enqueued transition rather than the button press
- [ ] write Unity test asserting `ButtonEvent`s route to the correct action per current screen
      state (home vs recording), independent of actual rendering
- [ ] manual on-device check: home ↔ recording screens work correctly via center button; face
      reacts to Recording/Saving/Idle events end to end
- [ ] run `logic_tests` — must pass before task 12

### Task 12: Main menu, Face submenu, theme persistence

**Files:**
- Create: `components/ui/screens/menu.c`
- Create: `components/ui/test/test_menu_nav.c`
- Modify: `components/ui/ui_task.c`
- Modify: `components/ui/CMakeLists.txt`

- [ ] Main menu per AGENTS.md §4.5 (New Recording / Recordings / Unsent / Send All / Face /
      Wi-Fi / Telegram / Settings / Info — the last 3 screens beyond navigation entries are
      stubs in v1.0 unless a later task fills them); Face submenu lists the 4 registered themes
- [ ] left=back, center=select/confirm, right=next/open, matching AGENTS.md §5
- [ ] selecting a theme calls `face_registry` to switch live (no reboot) and calls
      `config_save()` (Task 5) to persist `[face].theme` back to `recorder.ini`, satisfying
      "active theme is stored in configuration" — other menu-visited settings (Wi-Fi/Telegram)
      remain read-only/display-only in v1.0, not editable from the menu (not required by spec
      §4.5 beyond navigation)
- [ ] write Unity test for the menu navigation state machine (button sequence → expected
      screen/selection), independent of rendering
- [ ] manual on-device check: full menu walk-through; live theme switch with no reboot; power
      -cycle after switching and confirm the new theme persisted; unknown-theme-in-config still
      shows Minimal
- [ ] run `logic_tests` — must pass before task 13

### Task 13: Recordings / Unsent / Send All list screens

**Files:**
- Create: `components/ui/screens/recordings_list.c`
- Create: `components/ui/screens/unsent_list.c`
- Modify: `components/ui/ui_task.c`
- Modify: `components/ui/CMakeLists.txt`

- [ ] "Recordings": paged list of `/echo-pocket/rec/*.wav` (name + duration if known) — browse only, no
      playback in v1.0 (not in spec)
- [ ] "Unsent": paged list of queue entries in `pending`/`failed` state (reads
      `queue_store`, wired once Task 15 lands — stub with an empty list until then, note the
      dependency)
- [ ] "Send All": triggers the manual drain path that Task 17 implements; button here just
      dispatches the action, drain logic lives in `upload_task`
- [ ] write Unity test for list pagination logic (given N items and a page size, correct
      slicing/scroll bounds) — pure logic, no real SD needed
- [ ] manual on-device check: lists render and page correctly with 0, 1, and >1-page item counts
- [ ] run `logic_tests` — must pass before task 14

### Task 14: Wi-Fi manager + SNTP time sync

**Files:**
- Create: `components/network/CMakeLists.txt`
- Create: `components/network/include/wifi_manager.h`
- Create: `components/network/wifi_manager.c`
- Create: `components/network/net_selection.c`
- Create: `components/network/test/test_net_selection.c`
- Modify: `components/recorder/rec_id.c`

- [ ] connect using `[wifi_N]` entries from config, tried in order, auto-reconnect on drop;
      runs fully degraded (no crash, no blocking) when no network is reachable
- [ ] factor the "which network to try next" decision into a pure function
      `net_selection_next(cfg, last_index, last_result)` — test that directly; do NOT reach for
      an `esp_wifi` mocking framework for a ≤4-item ordered try-loop, that's disproportionate
      infrastructure for this logic
- [ ] on first successful connect, run SNTP and set the "time synced" flag `rec_id.c` (Task 7)
      checks, so subsequent recordings get real timestamps instead of the boot-relative fallback
- [ ] apply `[device].timezone` (Task 5) via `setenv("TZ", cfg.timezone, 1); tzset();` right after
      SNTP sync, so subsequent local-time formatting (filenames, captions) reflects the
      configured zone instead of silently staying UTC
- [ ] emits a connected/disconnected event other components (UI via `RECORDER_EVENTS`, upload
      task) subscribe to
- [ ] write Unity test for `net_selection_next` (given N configured networks and simulated
      connect results, assert correct try order and reconnect behavior) — no mocking framework,
      plain function with injected inputs
- [ ] manual on-device check: connects to the first working of 2 configured SSIDs; survives AP
      power-cycle and reconnects; SNTP-synced timestamp appears in the next recording's filename
- [ ] run `logic_tests` — must pass before task 15

### Task 15: Upload queue persistence and crash recovery

**Files:**
- Create: `components/storage/queue_store.c`
- Create: `components/storage/include/queue_store.h`
- Create: `components/storage/test/test_queue_store.c`
- Modify: `components/storage/CMakeLists.txt`
- Modify: `components/recorder/recorder.c`

- [ ] `/echo-pocket/queue/index.json` read/write with the schema from AGENTS.md §Upload queue
      (`id/file/state/duration_ms/size/attempts/telegram_message_id`)
- [ ] states: `recording → pending → uploading → sent | failed`; on boot, any `uploading`
      record is rewritten to `pending`
- [ ] never delete unsent files; queue file writes are atomic (write temp + rename, same
      pattern as Task 5's `config_save`) so a power loss mid-write can't corrupt the index
- [ ] `recorder.c` enqueues a `pending` entry right after the fsync+close in Task 7's finalize
      path (this is what the Recording screen's "Saved" state waits on, per Task 11)
- [ ] write Unity tests: round-trip serialize/deserialize; boot-time `uploading→pending`
      recovery; atomic write survives a simulated interrupted write (temp file present, real
      file untouched)
- [ ] run `logic_tests` — must pass before task 16

### Task 16: Telegram client (sendDocument, streamed from SD)

**Files:**
- Create: `components/telegram/CMakeLists.txt`
- Create: `components/telegram/include/telegram_client.h`
- Create: `components/telegram/telegram_client.c`
- Create: `components/telegram/test/test_caption.c`

- [ ] Bot API `sendDocument`, multipart/form-data, streams the WAV directly from SD (no full
      buffering in RAM), targets numeric `chat_id` or `@username`, honors `send_to_all`
- [ ] caption per AGENTS.md §Telegram (`Recorder ID / Duration / Device` — `Device` is
      `[device].name` from config)
- [ ] `getMe` helper for connectivity/token sanity check
- [ ] the largest file this device produces (18–20 min mono 16-bit 16kHz WAV, ~35–38MB) stays
      under the Bot API `sendDocument` size limit (50MB) with margin; add a runtime guard in the
      send path that checks the file size against a `TELEGRAM_MAX_UPLOAD_BYTES` constant before
      streaming and fails the attempt cleanly (queue entry stays `pending`/goes to `failed` per
      Task 17's normal failure path) rather than starting a doomed upload — cheaper and safer
      than relying on a comment nobody re-reads if sample rate or split length ever changes
- [ ] write Unity test for caption string formatting (given id/duration/device, exact expected
      string) — pure formatting logic
- [ ] manual on-device check: `getMe` succeeds with a real bot token; a small WAV and an
      18-minute WAV both upload successfully and appear in the target chat
- [ ] run `logic_tests` — must pass before task 17

### Task 17: Upload task — draining, retry cap, auto_upload/delete_after_upload

**Files:**
- Create: `components/network/upload_task.c`
- Create: `components/network/include/upload_task.h`
- Create: `components/network/test/test_upload_flow.c`
- Modify: `components/storage/queue_store.c`

- [ ] `upload_task`: on Wi-Fi-connected event (Task 14) or manual "Send All" (Task 13), drains
      `pending` items one at a time via the Telegram client (Task 16); does nothing if
      `[recorder].auto_upload=false` except in response to explicit "Send All"
- [ ] on send success (`ok: true`): store `telegram_message_id`, mark `sent`; if
      `[recorder].delete_after_upload=true`, delete the WAV file only after the queue write
      confirming `sent` has itself succeeded (never delete before the state is durably `sent`)
- [ ] on failure: increment `attempts`, revert to `pending` for retry (no backoff scheduler in
      v1.0); once `attempts` reaches a fixed cap (e.g. 5), mark `failed` instead — `failed` is
      terminal and only "Send All" resets a `failed` entry back to `pending`
- [ ] uploads never start mid-recording and never preempt an active recording
- [ ] write Unity test for the drain loop's state transitions given injected send
      success/failure sequences (plain function/struct, not a mocking framework) — asserts the
      attempts→`failed` cap and the `delete_after_upload` ordering
- [ ] manual on-device check: record 3 files offline, connect Wi-Fi, confirm all 3 drain in
      order and each gets a `sent` state + message id; force repeated failures and confirm the
      entry reaches `failed` and stays there until manual "Send All"
- [ ] run `logic_tests` — must pass before task 18

### Task 18: Battery/power status

**Files:**
- Create: `components/board/battery.c`
- Create: `components/board/include/battery.h`
- Create: `components/board/test/test_battery_curve.c`
- Modify: `components/ui/screens/home_screen.c`
- Modify: `components/network/upload_task.c`
- Modify: `components/recorder/recorder.c`

- [ ] branch on the hardware verdict recorded in Task 1 (`board.h`): if VBAT is not readable,
      show only `USB / Charging / Battery / Level unknown` — never derive a percentage from
      indirect signals (AGENTS.md §12.2, hard requirement)
- [ ] if VBAT is readable: calibrated ADC read, multi-sample average, divider ratio applied,
      updated every 10–30s, single-cell Li-ion discharge-curve lookup (non-linear), thresholds
      >20% normal / ≤20% warning / ≤10% block large auto-uploads (file size over a
      `LOW_BATTERY_UPLOAD_MAX_BYTES` constant, e.g. 5MB — roughly a 2-3 min recording) /
      critical → safe-stop
- [ ] ≤10% threshold sets a flag `upload_task.c` (Task 17) checks before starting an
      auto-upload whose file exceeds `LOW_BATTERY_UPLOAD_MAX_BYTES` (manual "Send All" still
      allowed regardless of size) — this is the actual enforcement point, not just a UI label
- [ ] critical-battery path calls the same finalize-then-stop function Task 7's normal stop and
      18–20 min auto-split already use (current audio block → WAV header patch → fsync/close →
      enqueue) — then powers down; no duplicated finalize logic
- [ ] publish `LowBattery` onto `RECORDER_EVENTS` (Task 9) so `ui_task` drives the face theme's
      `LowBattery` state
- [ ] write Unity test for the voltage→percent discharge-curve lookup and for the
      threshold-to-UI-state mapping (pure math/logic, no real ADC needed)
- [ ] run `logic_tests` — must pass before task 19

### Task 19: Boot recovery integration (SD mount → config → queue → resume)

**Files:**
- Modify: `main/app_main.c`

- [ ] wire the full boot order: mount SD (Task 4) → parse config (Task 5) → load queue +
      recover `uploading→pending` (Task 15) → start audio/UI/network/upload tasks — no data
      loss and no crash on any missing/corrupt piece
- [ ] no unit test here: an "assert app_main calls things in this order" test only restates the
      code as written and catches nothing real — the actual verification is the power-cycle
      drill in Post-Completion, which exercises real recovery behavior end to end
- [ ] manual on-device check (Post-Completion): power-cycle mid-upload and mid-recording,
      confirm recovery matches AGENTS.md §3.5 exactly

### Task 20: Verify v1.0 acceptance criteria

- [ ] walk every bullet in AGENTS.md §"v1.0 done-criteria (checklist)" against the running
      firmware and check it off explicitly in this plan's notes
- [ ] run the full `logic_tests` suite: `idf.py -C test_apps/logic_tests build flash monitor` —
      all green
- [ ] confirm out-of-scope list (AGENTS.md §"Explicitly out of scope for v1.0") has not crept
      into the implementation
- [ ] confirm audio capture was never blocked by SD/display/network at any point across all
      manual on-device checks logged above (ring-buffer overflow counter stayed at 0 except
      where explicitly noted)
- [ ] confirm every config key across ALL sections (`[device]`, `[wifi_N]`, `[telegram]`,
      `[recorder]`, `[face]`) documented in Task 5 as "consumed" is actually wired, and every key
      documented as "inert" is genuinely unused (no drift)

### Task 21: [Final] Update documentation

- [ ] update `AGENTS.md`/`CLAUDE.md` with any real pin numbers, component names, or
      architectural deviations discovered during implementation
- [ ] add a short `README.md` with build/flash instructions for both the main project and
      `test_apps/logic_tests` (`idf.py build`, `idf.py flash`, target chip, required
      `sdkconfig.defaults`)
- [ ] move this plan to `docs/plans/completed/`

## Post-Completion
*Requires the physical board — cannot be scripted or unit tested*

**Manual verification**:
- Board schematic / vendor example review to confirm VBAT-to-ADC exposure (done once, up front,
  as part of Task 1 — re-verify here only if Task 1's answer is later found wrong on real
  hardware).
- 10+ minute continuous recording soak test for dropouts (Task 7/8), including with the LCD and
  SD sharing an SPI bus if Task 4 found that to be the case.
- Real Telegram delivery for both a small and an 18–20 minute WAV, including over a flaky/slow
  connection (Task 16/17).
- Battery percentage vs a multimeter reading across a full charge/discharge cycle, including
  under Wi-Fi TX load (voltage sag).
- Power-loss-mid-recording and power-loss-mid-upload drills to confirm the boot recovery path
  (Task 19) never corrupts a WAV or loses a queue entry.
- Visual/usability pass on all 4 face themes and all menu screens on the actual 240×240 panel
  (color accuracy, legibility, animation smoothness at the configured `animation_fps`).

**External resources**:
- Telegram bot token + target channel(s) must be created and added to `recorder.ini` manually
  before Task 16's on-device checks.
- microSD card formatted FAT32 required for all storage-related on-device checks from Task 4
  onward (see Technical Details for the accepted SD wear-leveling risk).
