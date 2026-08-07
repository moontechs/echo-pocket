# Scoping: host-native (IDF_TARGET=linux) logic_tests

## Overview

`test_apps/logic_tests` builds pure-logic unit tests, but they're still
cross-compiled for `esp32s3`/xtensa and only run on real silicon over
serial (see `components/ui/test/test_menu_nav.c` and friends). There's no
way to run them on a Mac without a board attached.

ESP-IDF has an `IDF_TARGET=linux` build (currently gated behind
`--preview set-target linux` on the installed IDF v5.3), which compiles
and runs the same Unity test binary natively as a host executable — no
board, no QEMU. This doc scopes what it would take to move `logic_tests`
onto it. **This is a scoping document, not an implementation plan** — no
code changes here, just findings and a recommended path so a future
implementation plan can skip the discovery phase.

## Why this matters

- Tests currently require a physical ESP32-S3 + USB connection to run at
  all, which blocks running them in CI or by a contributor without
  hardware.
- The project already cherry-picks pure-logic `.c` files as loose sources
  in `test_apps/logic_tests/main/CMakeLists.txt`
  (`ui_screen_logic.c`, `screens/menu.c`, `audio_ringbuf.c`,
  `recorder_logic.c`, `queue_store.c`, `telegram_caption.c`, etc.)
  instead of pulling in their parent components (`ui`, `audio`,
  `recorder`, `telegram`) wholesale, specifically because those
  components require `esp_lcd`/`esp_driver_spi`/`esp_codec_dev` —
  hardware drivers that don't build outside a real target. Comments in
  `test_apps/logic_tests/CMakeLists.txt` document this pattern
  explicitly. Note: this is cherry-picking whole files that are *already*
  hardware-free, not splitting a mixed file — no file in that list has
  been surgically divided. The remaining blocker is that two components
  are still pulled in **wholesale** via `REQUIRES`: `board` and
  `storage`.

## Findings

### What's already target-agnostic
- `unity` and the loose pure-logic `.c` files listed above have no
  direct hardware calls (verified by grepping for
  `gpio_/adc_/i2s_/spi_` etc. — zero hits in `recorder_logic.c`,
  `ui_screen_logic.c`, `screens/menu.c`, `queue_store.c`,
  `telegram_caption.c`).
- The linux-target blockers are narrower than "nothing hardware-adjacent
  builds": `freertos` has a real POSIX port
  (`$IDF_PATH/components/freertos/{FreeRTOS-Kernel,FreeRTOS-Kernel-SMP}/portable/linux`),
  `esp_event` explicitly adds itself for linux
  (`components/esp_event/CMakeLists.txt`: `if(${target} STREQUAL
  "linux") list(APPEND requires "linux")`), and `fatfs` has a linux
  port (`port/linux/ffsystem.c`). None of those need to be removed.
  The actual blockers are `esp_driver_gpio`, `esp_adc`, and `driver`,
  which `return()` immediately in their `CMakeLists.txt` under the
  linux target ("This component is not supported by the POSIX/Linux
  simulator") — a component naming them in `REQUIRES` fails at
  resolution time, before any compiling happens.

### What's still hardware-coupled
- **`board` component** (`components/board/CMakeLists.txt`, `REQUIRES
  esp_driver_gpio freertos esp_event esp_adc`): bundles `board_pins.c`
  (pure GPIO init, no pure logic worth extracting), `buttons.c` (3
  `gpio_*` calls — button debounce is a pure state machine but the file
  also owns the ISR/GPIO init), and `battery.c` (11 `gpio_*`/`adc_*`
  calls — the discharge-curve math tested by
  `components/board/test/test_battery_curve.c` is pure). The
  `esp_driver_gpio`/`esp_adc` entries in `REQUIRES` are what actually
  blocks linux.
- **`storage` component** (`components/storage/CMakeLists.txt`,
  `REQUIRES fatfs driver board`): bundles `sd_storage.c` (3
  `sdmmc_*`/FATFS calls — `sd_storage_err_str()` and
  `sd_storage_is_mounted()`, both tested by `test_sd_storage.c`, are
  pure), `config.c` (zero hardware calls, no split needed — see below),
  and `queue_store.c` (already proven separable — the test app compiles
  it as a loose source independently). The `driver` entry (pulled in
  for `sd_storage.c`'s SDMMC use) and the `board` dependency are what
  blocks linux here; `fatfs` itself is fine.
- Net: only **two files** need actual internal splitting —
  `battery.c` and `buttons.c` — plus one **trivial** split,
  `sd_storage.c` (2 pure one-line functions vs. 3 real SDMMC calls).
  `config.c` needs no splitting at all: it has zero hardware calls,
  includes only its own header, libc, `esp_err.h`/`esp_log.h`, and
  never calls into `sd_storage.c`. It can be added as a loose source
  today, exactly like `queue_store.c` already is.

### Open question — resolved
`queue_store.c` (and, previously unnoticed, `telegram_caption.c`)
appear both as a loose source in `test_apps/logic_tests/main/CMakeLists.txt`
*and* inside their parent component's `SRCS`
(`components/storage/CMakeLists.txt`, `components/telegram/CMakeLists.txt`)
while that parent component is also in `REQUIRES`/`EXTRA_COMPONENT_DIRS`.
This does **not** cause a duplicate-symbol link error: `main` and
`storage`/`telegram` compile to separate static archives, and the
linker only pulls archive members it needs to resolve outstanding
symbols. Since `main` links first and already defines
`queue_serialize`/`telegram_caption_*` etc. from its own loose-source
object files, the archived copies in `libstorage.a`/`libtelegram.a`
are simply never pulled in — dead weight, not a conflict. No further
investigation needed before scoping the real migration; just don't
repeat the double-listing for newly split files.

## Options

**Option A: Split `battery.c`/`buttons.c`/`sd_storage.c` into hardware
vs. pure-logic files, no new components (recommended)**
- How: extract `battery_voltage_to_percent`/`battery_percent_to_threshold`/
  `battery_threshold_for_upload` out of `battery.c`; extract the button
  debounce state machine out of `buttons.c`; extract
  `sd_storage_err_str()`/`sd_storage_is_mounted()` out of
  `sd_storage.c`. Compile the three extracted pure files directly in
  the test app (same treatment `queue_store.c` already gets), add
  `config.c` as a loose source unchanged, and drop `REQUIRES board
  storage` from `test_apps/logic_tests/main/CMakeLists.txt`.
- Pros: extends the loose-source cherry-picking pattern the test app
  already relies on; no new abstraction, no new component; test app's
  `REQUIRES` shrinks to `unity` (+ whatever the split files still need,
  e.g. `log`).
- Cons: this is new work, not just "more of the same" — unlike the
  existing loose sources (which were already hardware-free files),
  `battery.c` and `buttons.c` today mix pure and impure code in one
  file and need actual internal separation. `sd_storage.c`'s split is
  trivial (2 one-line functions); `config.c` needs none.

**Option B: `idf.py --preview set-target linux` for the whole test app as-is**
- How: just flip the target and see what breaks.
- Pros: zero source changes if it happens to work.
- Cons: it won't — `board` names `esp_driver_gpio`/`esp_adc` and
  `storage` names `driver` in `REQUIRES`, and those components
  `return()` under the linux target, so CMake fails at component
  *resolution*, before compiling anything. No amount of splitting
  *inside* `battery.c`/`buttons.c`/`sd_storage.c` fixes this on its
  own — the test app's `REQUIRES board storage` line has to go too
  (Option A's last step).

**Option C: Add QEMU (xtensa-esp32s3) as the host-run target instead**
- How: run the existing cross-compiled test binary under Espressif's
  QEMU fork.
- Pros: no source restructuring — the current binary runs unmodified.
- Cons: heavier local setup (QEMU install/build), xtensa-esp32s3 QEMU
  support is newer/less mature than the linux target, and it still
  doesn't solve "no hardware drivers" — QEMU would need GPIO/ADC/SDMMC
  peripheral models too, a much bigger lift than splitting three files.

**Option D: Skip ESP-IDF's build system entirely for these tests**
- How: most loose-source files only include their own header plus
  libc (`recorder_logic.c` → `recorder_split.h`; `net_selection.c` →
  `net_selection.h`; `audio_ringbuf.c` → header + `stdlib`/`string`).
  Only a handful (`queue_store.c`, `config.c`, `wav_writer.c`) pull
  `esp_err.h`/`esp_log.h`, which are trivially shimmable in ~20 lines.
  Compile the pure files + a tiny Unity-alike (or vendored Unity) with
  plain `cc`/a Makefile — no `idf.py`, no sdkconfig, no `--preview`
  flag.
- Pros: strictly less machinery than Option A + linux target; fastest
  to iterate on; no ESP-IDF version dependency for running tests.
- Cons: loses `idf.py`'s FreeRTOS POSIX simulation if any future test
  needs real task/queue semantics rather than pure functions; a second,
  hand-rolled build path to maintain alongside the real firmware build.

**Recommendation: Option A**, on its own merits (not because it's "more
of the same" — it's the smallest change that keeps one build system).
It requires real work on exactly two files (`battery.c`, `buttons.c`)
plus a trivial third (`sd_storage.c`), stays inside `idf.py`/CMake so
there's no second build toolchain to maintain, and directly enables
`IDF_TARGET=linux` once IDF drops the `--preview` gate. Option D is a
reasonable runner-up if minimizing build-system surface matters more
than staying inside `idf.py` — worth revisiting if the linux target
turns out to have friction Option A didn't anticipate.

## Suggested shape of the follow-up implementation plan

Not written here (out of scope for a scoping doc), but based on these
findings it would look like:
1. Extract `battery_voltage_to_percent`/`battery_percent_to_threshold`/
   `battery_threshold_for_upload` out of `battery.c` into a pure file;
   compile it as a loose source instead of relying on `REQUIRES board`.
2. Extract the button debounce state machine out of `buttons.c`;
   compile it as a loose source.
3. Extract `sd_storage_err_str()`/`sd_storage_is_mounted()` out of
   `sd_storage.c` (trivial — 2 functions, no real logic change).
4. Add `config.c` as a loose source in
   `test_apps/logic_tests/main/CMakeLists.txt` (no code change needed).
5. Drop `REQUIRES board storage` from
   `test_apps/logic_tests/main/CMakeLists.txt`; add the four split-out
   pure files as loose sources instead. Don't re-list them in a parent
   component's `SRCS` the way `queue_store.c`/`telegram_caption.c` are
   today — harmless (dead archive members, per the resolved open
   question above) but avoid growing the pattern.
6. Add a second build config / CI job: `idf.py --preview set-target
   linux build`, run the resulting host binary directly (no
   flash/monitor).
7. Confirm no remaining test in `logic_tests` needs real hardware after
   the split — expected to be true given "logic_tests" is meant to be
   hardware-free by design, but verify by grepping the final loose
   source + `REQUIRES` list for anything under `esp_driver_*`/`esp_adc`.

## Post-Completion
*Not applicable — this is a scoping document with no implementation
tasks. See "Suggested shape of the follow-up implementation plan" above
for what a real plan would contain.*
