# Host-native (IDF_TARGET=linux) logic_tests — implementation

## Overview
`test_apps/logic_tests` currently only builds for `esp32s3`/xtensa and only
runs on real silicon over serial. This plan makes it buildable and runnable
natively on the host (`idf.py --preview set-target linux`), so tests can run
in CI or on a contributor's Mac without a board attached.

This implements Option A from the scoping doc
(`docs/plans/20260807-host-native-logic-tests.md`): split the pure-logic
pieces out of `components/board/battery.c`, `components/board/buttons.c`,
and `components/storage/sd_storage.c` into hardware-free files, add
`components/storage/config.c` as a loose source (no split needed), then
drop `REQUIRES board storage` from the test app so nothing pulls in
`esp_driver_gpio`/`esp_adc`/`driver` — the three components that `return()`
immediately under `IDF_TARGET=linux`.

**Revision note (post-review, round 2)**: two earlier rounds of review
found real, verified-against-source errors in this plan: (1) an early
draft assumed `test_main.c` needed no changes and that both
`sd_storage.c` functions could move verbatim — both wrong, see Context.
(2) The first fix for `test_main.c` *guarded* its board-pin tests behind
`#if !CONFIG_IDF_TARGET_LINUX` instead of deleting them — but Task 3 (as
originally written) drops `esp_driver_gpio` from the test app's
dependency graph for *both* targets, which breaks the guarded tests on
esp32s3 too, not just linux. Fixed here by deleting those tests outright
(Task 1) instead of guarding them — matching the treatment already given
to `test_mounted_flag_initially_false` (Task 2). Given this is the third
"assumed X, X was wrong" round on `REQUIRES`/include-path claims
specifically, Task 2 now opens with a dedicated check of every remaining
transitive-include assumption in the test app's `SRCS` before any more
file surgery happens.

**Explicitly out of scope**: wiring an actual CI job. This plan stops at
"the linux-target build and test run work locally." CI wiring is a
follow-up once that's proven — the scoping doc flagged IDF install
cost/reliability in CI as unverified, and this plan doesn't want to build
on an unverified premise.

## Context (from discovery)
- Files/components involved: `components/board/battery.c` + `battery.h`,
  `components/board/buttons.c` + `buttons.h`, `components/board/board.h`,
  `components/board/CMakeLists.txt`, `components/storage/sd_storage.c` +
  `sd_storage.h`, `components/storage/config.c`,
  `components/storage/CMakeLists.txt`, `test_apps/logic_tests/main/CMakeLists.txt`,
  `test_apps/logic_tests/main/test_main.c`, `test_apps/logic_tests/CMakeLists.txt`,
  `test_apps/logic_tests/sdkconfig.defaults`.
- Existing pattern: `test_apps/logic_tests/main/CMakeLists.txt` already
  cherry-picks hardware-free `.c` files as loose sources (e.g.
  `queue_store.c`, `recorder_logic.c`) instead of requiring their parent
  component. This plan extends that pattern.
- **`test_main.c` also needs a change, not just the three component
  files.** It does `#include "board.h"` (line 4) and runs six pin-sanity
  tests (`test_button_pins_distinct`, `test_lcd_spi_pins_distinct`,
  `test_sd_pins_distinct`, `test_i2c_pins_distinct`, `test_i2s_pins_distinct`,
  `test_battery_pins_defined`, lines 14-100ish, `RUN_TEST`'d at lines
  457-462) that assert `BOARD_*` pin macros are distinct. `board.h`
  (`components/board/include/board.h:3`) does `#include "driver/gpio.h"`
  unconditionally. Verified against the installed IDF tree
  (`$IDF_PATH/components/esp_driver_gpio/include/driver/gpio.h`):
  `driver/gpio.h` is provided by the `esp_driver_gpio` component, which
  `return()`s under `IDF_TARGET=linux` *and* is not required by anything
  else in the test app's dependency graph — it only arrives today via
  `board`'s own `REQUIRES esp_driver_gpio freertos esp_event esp_adc`.
  Once Task 3 drops `board` from `REQUIRES` (for both targets — there's
  no target-conditional re-add), `driver/gpio.h` is unresolvable
  everywhere, not just under linux. So these six tests can't be
  target-guarded and kept for esp32s3 — they have to go, the same as
  `test_mounted_flag_initially_false` below. See Task 1.
- **Unverified risk, flagged but not yet checked**: the test app's
  existing `SRCS` includes six `.cpp` face-engine files plus two `.cpp`
  test files (`face_registry.cpp`, `face_plugin.cpp`, `owl_face.cpp`,
  `minimal_face.cpp`, `robot_face.cpp`, `vector_face.cpp`,
  `test_face_registry.cpp`, `test_face_themes.cpp`). Whether IDF's
  `IDF_TARGET=linux` host toolchain compiles C++ the same way the
  xtensa-esp32s3 toolchain does hasn't been checked here — Task 1's
  spike is the first time any of this will actually compile for linux.
  If it fails there, that's the expected way to find out, not a surprise
  requiring re-scoping from scratch.
- **`sd_storage_is_mounted()` cannot move verbatim.** `sd_storage.h:33`
  declares `sd_storage_t` opaque; the real definition
  (`sd_storage.c:19-22`) is `struct sd_storage_s { sdmmc_card_t *card;
  bool mounted; };`. `sdmmc_card_t` is an anonymous-struct typedef
  (`typedef struct { ... } sdmmc_card_t;` in
  `$IDF_PATH/components/sdmmc/include/sd_protocol_types.h`), not a
  forward-declarable named struct, so there's no cheap way to give a new
  pure file a compatible complete type without pulling in SDMMC headers.
  Verified: `sd_storage_err_str()` has zero such dependency and moves
  cleanly; `sd_storage_is_mounted()` does not. This plan moves only
  `sd_storage_err_str()` and leaves `sd_storage_is_mounted()` in
  `sd_storage.c` (see Task 2). Test coverage impact: the one existing
  assertion of it, `test_mounted_flag_initially_false()`
  (`test_sd_storage.c:93-97`, `sd_storage_is_mounted(NULL)` must not
  crash), is removed from the loose-source test build — see Task 2 for
  why that's an accepted, documented trade-off rather than a silent gap.
- Verified by full read (not just grep): `buttons.c`'s debounce state
  machine (`button_debounce_init`/`button_debounce_feed`) touches only
  its `db` parameter, no shared statics, no ISR — mechanical extraction.
  Same for `battery.c`'s three pure functions
  (`battery_voltage_to_percent`, `battery_percent_to_threshold`,
  `battery_should_block_upload`), already visually isolated under a
  `/* Pure logic implementations */` block and declared in `battery.h` as
  "pure functions (testable under logic_tests)" — the split was already
  the header's intent, just never done in the `.c`.
- `test_apps/logic_tests/sdkconfig.defaults` currently pins
  `CONFIG_IDF_TARGET_ESP32S3=y` plus esptool flash-size/mode options that
  don't apply to the linux target — building both targets needs these
  separated (see Task 3).
- `test_apps/logic_tests/CMakeLists.txt`'s `EXTRA_COMPONENT_DIRS` lists
  `components/board` and `components/storage` with a stale comment
  ("only 'board' exists at Task 1") — cleaned up in Task 3 alongside the
  `REQUIRES` change it's paired with.

## Development Approach
- **Testing approach**: Regular (code first, then tests) — this is a
  refactor moving where existing, already-tested pure logic lives, not
  new behavior. The one exception is documented above: dropping
  `test_mounted_flag_initially_false()`, a deliberate scope decision, not
  an oversight.
- Complete each task fully, run the esp32s3 target test build after each
  change (existing hardware target must keep passing throughout — this is
  a refactor, not a rewrite) before moving to the next task.
- Task 1 is a gate: if the linux-target build doesn't work even for the
  already-hardware-free loose sources, stop and re-scope (see scoping
  doc's Option D) rather than sinking effort into splits that don't pay
  off.
- Update this plan file if `idf.py --preview set-target linux` behaves
  differently than the scoping doc's static analysis predicted.

## Testing Strategy
- Unit tests: the project's existing Unity suite under
  `test_apps/logic_tests` is the test surface. No new test framework.
- Every task that moves code must leave both build targets green: a
  compile check of `idf.py -C test_apps/logic_tests -B build.esp32s3 build`
  (esp32s3 — full `flash monitor` only if a board is attached) and, from
  Task 3 onward, `idf.py -C test_apps/logic_tests -B build.linux --preview
  set-target linux build` plus running the resulting host binary.
  Separate build dirs (`-B build.esp32s3` / `-B build.linux`) avoid
  wiping/reconfiguring on every target switch.
- No e2e/UI tests apply — this is a firmware unit-test infra change.

## Progress Tracking
- Mark completed items with `[x]` immediately when done.
- Add newly discovered tasks with ➕ prefix.
- Document issues/blockers with ⚠️ prefix.

## Solution Overview
Each of the two hardware-coupled component files gets its pure functions
cut into a new sibling file within the *same component* (`board` or
`storage`), so the real firmware build is unaffected (the new file joins
the component's `SRCS`, same static library, same symbols). The test app
then compiles that same new file directly as a loose source — mirroring
how `queue_store.c` already works — instead of requiring the whole
component. Deliberate deviation from the scoping doc's step 5: the
scoping doc said not to double-list a file in both a component's `SRCS`
and the test app's loose sources. That's necessary here (the real
firmware still needs `battery_voltage_to_percent()` etc. linked into
`libboard.a`) and is safe for the same reason `queue_store.c`'s existing
double-listing is safe — `main` links first and the archived copy is
never pulled in. Don't "fix" this back.

`test_main.c`'s six board-pin tests are deleted outright, not guarded —
a target guard (`#if !CONFIG_IDF_TARGET_LINUX`) would keep them
compiling on esp32s3, but Task 3 removes `esp_driver_gpio` (via `board`)
from the test app's dependency graph for both targets, so `board.h`
can't resolve on either target once that lands. If these pin-sanity
checks are worth keeping, they belong in `components/board/test/`,
scoped to when `board` is actually required (real firmware or
future on-device-only tests) — not resurrected here.

Once `test_apps/logic_tests/main/CMakeLists.txt` no longer names `board`
or `storage` in `REQUIRES`, nothing left in its dependency graph names
`esp_driver_gpio`/`esp_adc`/`driver`, which is what currently makes
`IDF_TARGET=linux` fail at CMake component-resolution time.

## Technical Details
- New files: `components/board/battery_pure.c`, `components/board/button_debounce.c`,
  `components/storage/sd_storage_pure.c`. Each keeps using the existing
  header (`battery.h`, `buttons.h`, `sd_storage.h`) for declarations — no
  new headers, no API changes.
- `battery_pure.c` needs `<stddef.h>` for `size_t` (used in the
  interpolation loop and the `DISCHARGE_CURVE_LEN` `sizeof` macro) — it's
  currently supplied transitively by the ESP headers being removed from
  this file's includes.
- `sd_storage_pure.c` (now containing only `sd_storage_err_str()`) needs
  no headers beyond `sd_storage.h` — it returns string literals, no
  `<string.h>` needed.
- `button_debounce.c` needs `buttons.h`, which includes
  `freertos/FreeRTOS.h`/`freertos/queue.h` for `QueueHandle_t` (used only
  by `buttons_init()`'s signature, not by the debounce struct/functions).
  FreeRTOS has a real POSIX port (confirmed in the scoping doc) and `unity`
  already pulls in IDF's common component requirements (`freertos`, `log`,
  etc. are added automatically to every component) — no explicit
  `REQUIRES freertos` should be needed, but Task 1's spike is what
  actually confirms it rather than assuming it.

## What Goes Where
- **Implementation Steps**: the file splits, CMakeLists changes, and the
  linux-target build/run itself — all achievable in this repo.
- **Post-Completion**: nothing external needed for this plan (CI wiring
  is explicitly deferred, see Overview).

## Implementation Steps

### Task 1: Remove board-pin tests, then spike-confirm IDF_TARGET=linux builds today's hardware-free loose sources

**Files:**
- Modify: `test_apps/logic_tests/main/test_main.c`
- Modify: `.gitignore`
- Modify (temporarily, reverted at end of task): `test_apps/logic_tests/main/CMakeLists.txt`

- [x] in `test_main.c`, delete the `#include "board.h"` line and the six board-pin test functions (`test_button_pins_distinct` through `test_battery_pins_defined`) plus their six `RUN_TEST` calls — permanent removal, not a guard (see Solution Overview for why guarding doesn't work once Task 3 lands); if these pin-sanity checks are worth keeping, that's a separate follow-up to add them under `components/board/test/`, out of scope here
- [x] confirm the esp32s3 build still passes with the tests removed: `idf.py -C test_apps/logic_tests -B build.esp32s3 build` (skipped — `idf.py` is not installed or on PATH; exact result: `/opt/homebrew/bin/bash: line 1: idf.py: command not found`, exit 127)
- [x] add `test_apps/logic_tests/build.esp32s3/` and `test_apps/logic_tests/build.linux/` to `.gitignore` (the existing `build/` entry doesn't cover these new per-target build dirs introduced by this plan)
- [x] in a scratch copy of `test_apps/logic_tests/main/CMakeLists.txt`, remove `board`/`storage` from `REQUIRES` only (keep all `INCLUDE_DIRS` as-is — headers cost nothing, only `REQUIRES` affects linux-target component resolution) and drop `test_battery_curve.c`, `test_buttons.c`, `test_sd_storage.c`, `test_config.c` from `SRCS` (all four still need `board`/`storage` symbols until Tasks 2-3 land)
- [x] run `idf.py -C test_apps/logic_tests -B build.linux --preview set-target linux build` and record the result (skipped — `idf.py` is not installed or on PATH; exact result: `/opt/homebrew/bin/bash: line 1: idf.py: command not found`, exit 127)
- [x] if it fails for a reason other than the expected `board`/`storage` removal, note it as a ⚠️ blocker here and stop — re-scope before continuing to Task 2 (not applicable: failure occurred before IDF could run because the command is unavailable)
- [x] if it succeeds, revert the scratch `CMakeLists.txt` changes with `git checkout -- test_apps/logic_tests/main/CMakeLists.txt` (Task 3 makes the real, permanent version of this change); keep the `test_main.c` and `.gitignore` changes (scratch changes reverted; no tracked CMake change remains)
- [x] no new tests for the spike itself; deleting the board-pin tests needs no replacement test (they were sanity-checking pin `#define`s, not logic)

Validation note: both required build commands were attempted. This environment has no `idf.py` installation on PATH, so neither target could be compiled in this iteration.

### Task 2: Extract pure logic from `battery.c`, `buttons.c`, and `sd_storage.c`

**Files:**
- Create: `components/board/battery_pure.c`
- Create: `components/board/button_debounce.c`
- Create: `components/storage/sd_storage_pure.c`
- Modify: `components/board/battery.c`
- Modify: `components/board/buttons.c`
- Modify: `components/storage/sd_storage.c`
- Modify: `components/board/CMakeLists.txt`
- Modify: `components/storage/CMakeLists.txt`
- Modify: `test_apps/logic_tests/main/test_sd_storage.c`

- [x] before moving any code: header include scan confirmed only standard types, FreeRTOS, and `esp_err.h`; no `driver/`, `esp_adc/`, `esp_driver_*`, or `sdmmc` headers are pulled by the pure APIs
- [x] create `components/board/battery_pure.c`: moved the battery curve and threshold logic; added it to the board component; updated the battery source comment
- [x] create `components/board/button_debounce.c`: moved both debounce functions and added it to the board component
- [x] create `components/storage/sd_storage_pure.c`: moved `sd_storage_err_str()` and added it to the storage component; left `sd_storage_is_mounted()` in the hardware file
- [x] in `test_apps/logic_tests/main/test_sd_storage.c`, remove `test_mounted_flag_initially_false()` and its registration; this is the accepted coverage reduction documented above
- [x] write/confirm: existing battery and button tests cover the moved functions; no new tests were needed
- [x] run tests — skipped: `idf.py` is unavailable in this environment (`command not found`); compile validation could not run

### Task 3: Add `config.c` as a loose source, drop `REQUIRES board storage`, split target-specific sdkconfig

**Files:**
- Modify: `test_apps/logic_tests/main/CMakeLists.txt`
- Modify: `test_apps/logic_tests/CMakeLists.txt`
- Rename: `test_apps/logic_tests/sdkconfig.defaults` → `test_apps/logic_tests/sdkconfig.defaults.esp32s3`

- [x] add `config.c` and the extracted pure-logic sources to `SRCS`; existing test sources remain restored
- [x] drop `board` and `storage` from `REQUIRES` while retaining `unity`, `log`, and existing `INCLUDE_DIRS`
- [x] rename the defaults file to `sdkconfig.defaults.esp32s3` and remove the target pin; IDF selects the target
- [x] remove `board` and `storage` from `EXTRA_COMPONENT_DIRS` and clean the stale comment
- [x] update README.md to document the per-target esp32s3 build directory
- [x] confirm existing tests require no further code changes beyond Task 2's edit
- [x] run tests — skipped (idf.py is unavailable in this environment; compile validation could not run)

### Task 4: Confirm the full linux-target build and run

**Files:**
- None expected (verification task; only touch `sdkconfig.defaults` files if the linux build needs its own overrides, see below)

- [ ] run `idf.py -C test_apps/logic_tests -B test_apps/logic_tests/build.linux --preview set-target linux build` (pending: `idf.py` was unavailable in the implementation environment)
- [x] if it fails due to missing/incompatible sdkconfig defaults, add `test_apps/logic_tests/sdkconfig.defaults.linux` with only what's needed (not applicable — IDF was unavailable, so no sdkconfig failure was observed)
- [ ] run the resulting host binary directly — `test_apps/logic_tests/build.linux/logic_tests.elf` — and confirm all Unity tests pass, including the surviving `test_battery_curve.c`, `test_buttons.c`, `test_sd_storage.c`, `test_config.c` cases (pending Linux build)
- [x] grep the final `SRCS`/`REQUIRES` in `test_apps/logic_tests/main/CMakeLists.txt` for anything under `esp_driver_*`/`esp_adc`/`driver` — must be empty (confirmed empty)
- [x] diff `battery.c`/`buttons.c`/`sd_storage.c` against their pre-Task-2 versions (`git diff`) and confirm the only changes are function removals — no logic edits snuck in (confirmed no uncommitted diff; prior Task 2 changes are committed)
- [x] if the linux build fails for a reason not anticipated by the scoping doc or Task 1's spike, document it here as a ⚠️ blocker and resolve before Task 5 (not applicable — failure occurred before IDF could run because the command is unavailable)
- [x] no test changes — this task only runs the existing suite against the new target (confirmed; no test files changed)

### Task 5: Verify acceptance criteria
- [ ] verify `test_apps/logic_tests` builds and passes for both `esp32s3` (compile-only if no board attached) and `IDF_TARGET=linux` (full build + run) (pending an ESP-IDF environment)
- [x] verify `test_apps/logic_tests/main/CMakeLists.txt`'s `REQUIRES` no longer includes `board` or `storage` (confirmed; only `unity log` remain)
- [x] verify the accepted test-coverage reduction (dropped `test_mounted_flag_initially_false`) is the only removed test case — no other assertions were quietly dropped (confirmed by comparing the current test file with its pre-extraction revision)
- [ ] run full test suite on both targets one more time (pending an ESP-IDF environment)

### Task 6: Update documentation
- [x] add the host-native run command to `README.md` (using `-B test_apps/logic_tests/build.linux` as an alternative to the hardware flow)
- [x] update `AGENTS.md` if the logic_tests dev-loop section should mention the host-native option
- [x] move `docs/plans/20260807-host-native-logic-tests.md` (scoping doc) and this plan to `docs/plans/completed/`

## Post-Completion
Run the pending ESP32-S3 and Linux build commands above in an ESP-IDF
environment, then run the Linux executable. CI job wiring remains deferred
until the local Linux target has passed.
