# ESP-SR AFE investigation — 2026-08-09

Session goal: wire up real ESP-SR AFE (NS/VAD/AGC), currently dead code
(`HAS_ESP_SR` always evaluates to 0 at build time, so every recording uses
the no-AFE downmix fallback, silently, on every build to date).

**Update (same day, continued session): the AFE fix described below was
re-applied, hardened, and landed for real this time.** Board is flashed
and confirmed working — real recordings via UI, uploaded to Telegram,
across multiple record/stop cycles, no crashes. See "What actually shipped"
below for the final, current state of the tree.

## Root cause found (real, confirmed)

`components/audio/CMakeLists.txt` never listed `espressif__esp-sr` in
`REQUIRES`. Only `main/idf_component.yml` depends on it, which makes esp-sr
a dependency of `main`, not of `audio` — so `esp_afe_sr_iface.h`'s include
path never propagated to `audio_process.c`, and
`#if __has_include("esp_afe_sr_iface.h")` has silently taken the `0`
branch on every build since this code was written. Confirmed via
`compile_commands.json`: zero `-I.../esp-sr` flags on `audio_process.c.obj`.

Fix: add `espressif__esp-sr` to `REQUIRES` in
`components/audio/CMakeLists.txt`. **Shipped, confirmed working.**

## What else had to change once the header was actually visible

Once esp-sr's real headers compiled, `audio_process.c`'s AFE integration
didn't match the vendored API at all (`espressif/esp-sr >=2.0,<3.0` per
`main/idf_component.yml`) — it was written against an older/different
esp-sr API shape and had never been build-tested:

- `esp_afe_handle_from_config()` returns a **vtable**
  (`esp_afe_sr_iface_t*`), not a data handle. Need
  `iface->create_from_config(&cfg)` to get the actual
  `esp_afe_sr_data_t*` instance, then pass *that* to `feed`/`fetch`/
  `get_channel_num` — not the vtable itself.
- `afe_config_t` field set is different: no `voice_communication_init`
  (now `afe_type = AFE_TYPE_SR | AFE_TYPE_VC | ...`); no `se_mode`; `agc_mode`
  is `afe_agc_mode_t` (`WEBRTC`/`WAKENET`), not `AFE_MODE_HIGH_PERF`;
  `memory_alloc_mode` enum renamed (`AFE_MEMORY_ALLOC_MORE_PSRAM`); `afe_mode`
  moved out of `pcm_config` to the top level.
- NS is `ns_init` (+ `ns_model_name`), not `se_init`. `se_init` is
  mic-array/beamforming (needs defined array geometry via `mic_ids`) —
  wrong field for "reduce background noise" on this 2-mic, non-array setup.
- `AFE_VAD_SPEECH` is a deprecated enum; real comparison is
  `res->vad_state == VAD_SPEECH` (`vad_state_t`).
- Missing `#include <string.h>` for `memcpy`/`memset`.
- `esp_afe_handle_from_config()` itself is declared in
  `esp_afe_sr_models.h`, not `esp_afe_sr_iface.h` — needed both includes.
- **(Found only in the second pass, see below)** Hand-building `afe_config_t`
  field-by-field is not enough — `afe_config_init()` must be called first to
  get a base config with the model-lookup fields populated, or `afe_feed()`
  null-derefs inside `afe_parse_input()`.

All of the above were fixed and the firmware built + flashed + booted
successfully, with the AFE pipeline confirmed live in the boot log
(`AFE Version: (2MIC_V251128)`, `VAD(WebRTC)` block present).

## Partition table

esp-sr's own `CMakeLists.txt` expects a partition named `model` when
`CONFIG_PARTITION_TABLE_CUSTOM=y` (it is, here) to pack/flash
`srmodels.bin`. `partitions.csv` didn't have one — added:

```
model,    data, spiffs,   ,        0x1F0000,
```
(placed in the ~1.9MB unused tail of 16MB flash, after `storage`).
**Shipped, confirmed working** — actual partition table on-device reads
`model  data  spiffs  0x1 82  0xe10000  0x1f0000` (auto-placed after
`storage`, matches).

Note: esp-sr's own CMake has a latent bug — on the "no model partition
found" path it builds a `message` variable but never calls `message()` on
it, so that failure is completely silent in build output. Not fixed
(upstream issue, not ours), just worth remembering if `srmodels.bin`
mysteriously doesn't flash in the future.

## A/B audio test — now actually done (first pass in the previous
write-up was inconclusive/blocked; this time it wasn't needed — see below)

Rather than pull WAVs off the SD card via the flaky Mac reader (previous
session's approach, abandoned), this pass used a Python pyserial script
reading `/dev/cu.usbmodem11101` directly and reset via DTR/RTS toggling —
avoided `idf.py monitor` entirely (it requires a real TTY, doesn't work
piped through the agent's bash tool). This was fast, reliable, and is the
recommended approach for future sessions: no SD card round-trip needed to
confirm recordings happened (queue_store / wav_writer log lines are
enough for pass/fail), and for actually judging *audio quality* the human
listened on-device (the recording is uploaded to Telegram automatically).

## Two new, real bugs found and fixed in this pass (not in the original
write-up, found only once AFE was actually exercised on real hardware)

### 1. AFE feed/fetch chunk size mismatch — real memory-safety bug, not just quality

`AUDIO_PROCESS_CHUNK_FRAMES` was hardcoded to 256 (16 ms @ 16 kHz,
matching the capture task's own read chunk — but there's no requirement
those two match). The AFE instance's actual required chunk size, queried
via `get_feed_chunksize()`/`get_fetch_chunksize()` and logged at init, was
**512**, not 256, for both the WebRTC and nsnet2 NS models.

Feeding a 256-frame stack buffer to an AFE `feed()` call that reads 512
frames from the pointer is a stack **buffer overread** — not merely
"wrong chunking", it reads adjacent stack memory as audio input. This was
present the *entire* time AFE was active, including in the first
"confirmed working" test — that test's WAV was never listened to closely
for quality, so this went unnoticed until testing the nsnet2 NS model,
where it produced constant, extremely obvious tonal artifacts
("just a beep") that the WebRTC algorithm apparently either partially
masked or was less sensitive to. **Real hardware audio testing (not just
"does it crash / does a file get written") is what caught this — build
success and a boot log are not sufficient signal for AFE correctness.**

Fixed: bumped `AUDIO_PROCESS_CHUNK_FRAMES` to 512 in
`components/audio/include/audio_process.h`, matching the AFE's actual
required chunk size. **Shipped.**

### 2. Process task stack overflow after the chunk-size fix

Bumping the chunk size to 512 doubled the process task's local stack
buffers (`stereo_buf`/`mono_buf`, ~3 KB combined) against a 4096-byte
task stack (`AUDIO_PROCESS_TASK_STACK_SIZE`) that also has to cover the
AFE/nsnet2 call chain (feed → afe_feed → neural-net inference). Result:
`Guru Meditation Error: Core 0 panic'ed (Double exception)` — a stack
overflow, immediately on process-task start, 100% reproducible.

Fixed: bumped `AUDIO_PROCESS_TASK_STACK_SIZE` to 16384. This is cheap
because (see below) the process task's stack now lives in PSRAM, not
internal RAM. **Shipped.**

### nsnet2 (neural-net NS model) — tried, does not work, reverted

Tried switching from the default WebRTC NS algorithm to the deep-learning
`nsnet2` model (`CONFIG_SR_NSN_NSNET2` in Kconfig/`sdkconfig`, packs a
~330 KB model into `srmodels.bin` instead of the near-empty 4-byte file
WebRTC needs). Even *after* fixing both real bugs above (chunk size +
stack overflow), nsnet2's output was a constant, obvious tone/whine — not
usable. Root cause not fully diagnosed (candidates: RNN/LSTM internal
state doesn't tolerate the feed gaps our ring-buffer-with-`vTaskDelay`
architecture introduces when data isn't immediately available; wrong
`afe_type`/model warm-up requirement; something else nsnet2-specific).

**Isolated via A/B**: reverted to `CONFIG_SR_NSN_WEBRTC` with the two
real fixes above still in place → clean audio, no whine, confirmed by
the user ("норм"). So the two real bugs were general AFE bugs (would
have affected WebRTC too, just less audibly), and nsnet2 is a separate,
still-unexplained problem on top. **Current shipped state: WebRTC NS,
not nsnet2.** `CONFIG_SR_NSN_NSNET2` is available in `sdkconfig` as a
toggle for a future attempt, but don't re-enable it without expecting the
whine to come back — this needs actual root-causing (chunk timing /
continuity investigation), not just re-flashing.

**Still not independently confirmed**: whether WebRTC NS is *measurably*
reducing background noise vs. no NS at all, beyond "doesn't sound broken
and the user is satisfied". No spectral/loudness comparison was done —
this was a subjective on-device listen only.

## Pre-existing bug: audio_capture ESP_ERR_NO_MEM — root-caused and fixed this pass

`audio_capture_start()` (`components/audio/audio_capture.c`) was creating
the capture task with plain `xTaskCreate()`, stack size 6144 bytes, from
internal RAM only:

```
E audio_cap: xTaskCreate failed
E recorder: audio_capture_start failed: ESP_ERR_NO_MEM
```

Root cause (confirmed previous session via heap probe, reconfirmed this
session by reproducing on demand): WiFi/TLS (Telegram client) fragments
internal RAM over device uptime. Total internal free RAM is greater than
6144 bytes, but no single contiguous block is big enough. Not a leak —
task is created/destroyed once per recording, so it recurs on every
recording once fragmentation sets in, not just once per boot.

**Fix that was previously attempted and reverted (100% boot crash-loop,
see old write-up)**: moving the stack to PSRAM via `xTaskCreateStatic()`
plus a **`static StaticTask_t`** TCB. The `static` TCB grew `.bss` by a
few hundred bytes, which was enough to break `display_init()`'s already
razor-thin internal-RAM budget (~77 KB of DMA buffers out of ~158 KB
total internal heap at boot) — 100% reproducible boot assert.

**Fix that actually worked this time**: same PSRAM-stack approach, but
the `StaticTask_t` TCB is `heap_caps_malloc(sizeof(StaticTask_t),
MALLOC_CAP_INTERNAL)`'d fresh on every `audio_capture_start()` /
`audio_process_start()` call and `heap_caps_free()`'d on stop — not
`static`. This keeps `.bss` unchanged (only a 4-byte pointer is `static`,
not the struct itself), so `display_init()`'s boot-time budget is
untouched. Confirmed: clean boot, no asserts, `idf.py size` not
regressed in a way that mattered, three back-to-back recordings with no
`ESP_ERR_NO_MEM`.

**Same underlying bug recurred in a second location once the capture-task
fix went in**: `audio_process_start()` (the *AFE* task, not the capture
task) has the exact same pattern (`xTaskCreate`, internal-RAM stack) and
started failing with `ESP_ERR_NO_MEM` right after the capture-task fix
freed up enough internal RAM for capture to succeed but not enough for
both tasks. Applied the identical fix there
(`components/audio/audio_process.c`): PSRAM stack via
`xTaskCreateStatic()`, non-`static` heap-allocated TCB. **Shipped,
confirmed working** — this is the fix that also enabled bumping
`AUDIO_PROCESS_TASK_STACK_SIZE` to 16384 cheaply (see above), since the
stack is no longer competing for scarce internal RAM.

**Lesson for next session, reconfirmed**: any fix that touches
`static`/global state must be boot-tested (fresh reset, check for
`assert failed` / `RTC_SW_CPU_RST` loops), not just tested for the
feature being changed — this bit twice now (once last session on
`display_init`, again implicitly this session in the sense that the same
"don't use `static` for TCBs" fix had to be duplicated correctly the
second time it was needed).

## What actually shipped (current tree state as of this write-up)

- `components/audio/CMakeLists.txt`: `espressif__esp-sr` added to
  `REQUIRES`.
- `partitions.csv`: `model` partition added.
- `components/audio/audio_process.c`: AFE integration rewritten against
  the real esp-sr 2.x API, using `afe_config_init()` +
  `esp_srmodel_init()` as the config base (not a hand-built
  `afe_config_t`); process task now uses `xTaskCreateStatic()` with a
  PSRAM stack and a non-static heap-allocated TCB.
- `components/audio/include/audio_process.h`:
  `AUDIO_PROCESS_CHUNK_FRAMES` 256 → 512 (matches AFE's real chunk size);
  `AUDIO_PROCESS_TASK_STACK_SIZE` 4096 → 16384 (now cheap, PSRAM-backed).
- `components/audio/audio_capture.c`: capture task now uses
  `xTaskCreateStatic()` with a PSRAM stack and a non-static
  heap-allocated TCB (same pattern as audio_process.c).
- `sdkconfig`: `CONFIG_SR_NSN_WEBRTC=y` (nsnet2 tried, reverted — see
  above).

Verified on real hardware: clean boot (no asserts, no crash-loop), NS/VAD
pipeline live in boot log (`AFE Pipeline: [input] -> |NS(WebRTC)| ->
|VAD(WebRTC)| -> [output]`), multiple record/stop cycles with no
`ESP_ERR_NO_MEM`/panics, WAV files written and finalized correctly,
recordings enqueued and successfully uploaded via Telegram, audio quality
judged acceptable by the user on-device.

## Open items for next session

1. nsnet2 whine — not root-caused, just avoided by reverting to WebRTC.
   If deep-learning NS is wanted later, needs real investigation (feed
   continuity, warm-up, `afe_type` choice), not a retry.
2. No objective (spectral/loudness) A/B was done for WebRTC-NS-on vs.
   NS-off — only subjective on-device listening. Fine for now, but if NS
   effectiveness is ever questioned, this is still open.
3. Unrelated bug found during this session's testing, **root-caused and
   fixed**: the face engine's eye size (driven by voice level during
   recording) did not reset to its default size when the UI returned to
   idle after a recording was sent. Root cause: `audio_process_stop()`
   already zeroed `s_voice_active` on stop (with a comment explaining
   why) but never zeroed the sibling `s_voice_level` static — so
   `audio_process_get_voice_level()` kept returning the last in-recording
   RMS value forever after stop, and `VectorFace::update()`
   (`components/ui/face_engine/vector_face.cpp`) correctly lerps toward
   that stale nonzero target (the face engine itself has no bug). Fixed
   by adding `s_voice_level = 0.0f;` next to the existing
   `s_voice_active = false;` in `audio_process_stop()`
   (`components/audio/audio_process.c`).
