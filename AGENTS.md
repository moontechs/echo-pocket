# AGENTS.md — ESP32-S3 Voice Recorder (echo-pocket)

Autonomous voice recorder firmware for the **Waveshare ESP32-S3-LCD-1.54** board (no touch).
Records voice to microSD with AI noise suppression, shows an animated reactive face on a 240×240
LCD, controlled by 3 buttons, auto-uploads recordings to Telegram with a persistent retry queue.
All config (Wi-Fi, Telegram, channels) lives in a plain-text INI file on the SD card.

Full requirements: this file. Source spec was written in Russian by the project owner; keep
terminology below in sync with it if the spec changes.

## Hardware

- ESP32-S3R8 (octal PSRAM, 8MB, 80MHz) + 16MB built-in flash
- 1.54" LCD, 240×240, ST7789 over SPI, no touch
- 2× microphones → ES7210 audio codec (I²C addr 0x40) → I²S
- microSD / TF card — SDMMC 4-bit (not SPI; no bus sharing with LCD)
- Wi-Fi 2.4GHz, BLE 5
- 3 physical buttons (left / center / right), active-low
- Single-cell 3.7V Li-ion/LiPo, onboard charge circuit
  - VBAT **is** readable via resistor divider on GPIO 1 (ADC1_CH0) — confirmed from vendor
    example `bsp_power_manager.c`
  - Charger status readable on GPIO 3 (low = charging)
  - GPIO 2 can cut battery power (low = off) for critical-battery shutdown

### Pin map (confirmed from vendor ESP-IDF example `waveshareteam/ESP32-S3-Touch-LCD-1.54`)

| Function      | GPIO | Notes                          |
|---------------|------|--------------------------------|
| LCD MOSI      | 39   | SPI2_HOST                      |
| LCD SCLK      | 38   |                                |
| LCD CS        | 21   |                                |
| LCD DC        | 45   |                                |
| LCD RST       | 40   |                                |
| LCD BL        | 46   | Backlight                      |
| BTN Left      | 0    | Active-low (strapping pin)     |
| BTN Center    | 5    | Active-low                     |
| BTN Right     | 4    | Active-low                     |
| SD CLK        | 16   | SDMMC 4-bit                    |
| SD CMD        | 15   |                                |
| SD D0         | 17   |                                |
| SD D1         | 18   |                                |
| SD D2         | 13   |                                |
| SD D3         | 14   |                                |
| I2C SDA       | 42   | ES7210 codec                   |
| I2C SCL       | 41   |                                |
| I2S MCK       | 8    |                                |
| I2S BCK       | 9    |                                |
| I2S WS        | 10   |                                |
| I2S DIN       | 11   | Mic data in                    |
| I2S DOUT      | 12   |                                |
| PA CTRL       | 7    | Power amplifier control        |
| BAT ADC       | 1    | ADC1_CH0, VBAT via divider     |
| BAT CHG       | 3    | Low = charging                 |
| BAT PWR       | 2    | Output, high = on              |

## Toolchain

- **ESP-IDF** (v5.x), not Arduino
- Component layout under `components/`, app entry in `main/`
- Unit tests in `test_apps/logic_tests/` (separate ESP-IDF project via `EXTRA_COMPONENT_DIRS`)
- Build command: `idf.py build`; flash: `idf.py flash monitor`
- Test command: `idf.py -C test_apps/logic_tests build flash monitor`
- ESP-IDF isn't always pre-installed in an agent sandbox — check for `idf.py` under a
  scratch path (e.g. `/private/tmp`) before assuming it's missing, then
  `source $IDF_PATH/export.sh`.
- `idf.py monitor` requires an attached TTY and fails outright in non-interactive/agent
  environments ("Monitor requires standard input to be attached to TTY"). To capture
  serial output non-interactively instead: `stty -f <port> 115200 cs8 -cstopb -parenb
  raw -echo && timeout <N> cat <port> > file`. Opening the port this way resets the
  board (DTR/RTS toggle), so the capture window starts from a fresh boot.
- Known gaps in `test_apps/logic_tests` (pre-existing, not yet root-caused):
  `test_upload_flow.c`'s target (`upload_drain_compute_outcome`) only exists in
  `upload_task.c`, which was never added to the test app's `SRCS` — wiring it in pulls
  a large dependency chain (Wi-Fi/telegram/recorder) not yet worked through, so those
  tests are currently excluded from the suite.

```
main/
  app_main.c               # Boot sequence wiring all components
components/
  board/                   # Pin defs, board_init, buttons, battery, device_events.h
  audio/                   # I2S capture, PSRAM ring buffer, ESP-SR AFE, voice level
  recorder/                # WAV writer, recording state machine, rec_id, split logic
  ui/
    face_engine/           # FacePlugin interface + registry + 4 built-in themes
    screens/               # Home, recording, menu, list screens
  storage/                 # SD mount, config parser + write-back, queue persistence
  network/                 # Wi-Fi manager, net_selection, upload task
  telegram/                # Bot API client (sendDocument), caption formatting
test_apps/
  logic_tests/             # Unity-based unit tests (pure logic, no hardware)
```

FreeRTOS tasks (in priority order, highest first — see `audio_capture.h`):
`audio_capture_task` > `audio_process_task` / `sd_writer_task` > `ui_task` >
`wifi_task` / `upload_task`.
Audio capture must never block on SD, display, or network — it writes into a PSRAM ring
buffer and nothing else.

## Audio pipeline

```
2× mic → ES7210 → I2S (2ch, s16, 16kHz) → PSRAM ring buffer (Task 6)
    → audio_process_task: ESP-SR AFE (NS + VAD + AGC) → mono PCM (Task 8)
    → sd_writer_task: mono PCM → WAV on SD (Task 7)
```

The ring buffer sits between capture and the AFE, not after it. Capture writes 2-channel
frames into the ring buffer and never blocks; the AFE and SD writer are both downstream
consumers. Overflow is counted and surfaced — never silently dropped.

- Format: WAV, PCM s16, mono, 16kHz (~1.92 MB/min)
- Use Espressif **ESP-SR** for noise suppression in v1.0 (built-in, not neural-from-scratch)
- Enable: noise suppression, VAD, moderate AGC (individually gated on config keys
  `[recorder].noise_suppression` / `[recorder].voice_detection`)
- Do NOT enable in v1.0: WakeNet, command recognition, playback during recording, AEC
- Does not promise removal of: other speakers, music, TV, sharp impact noise, wind, mic clipping
- Split files at ~18–20 min; auto-continue into a new WAV on limit

## Recording lifecycle (must be crash-safe)

On stop (including low-battery forced stop): close PCM stream → patch WAV header → flush/fsync →
close file → append to upload queue → **only then** show "Saved". Never skip fsync to save time.

All app data lives under one root folder on the SD card, `/echo-pocket/`, so the card can hold
other files/apps without collision:
```
/echo-pocket/config/recorder.ini
/echo-pocket/rec/REC_YYYYMMDD_HHMMSS_NNN.wav
/echo-pocket/queue/index.json
/echo-pocket/logs/
```

## Upload queue (`/echo-pocket/queue/index.json`)

States: `recording → pending → uploading → sent | failed`

```json
{
  "id": "REC_20260804_215700_001",
  "file": "/echo-pocket/rec/REC_20260804_215700_001.wav",
  "state": "pending",
  "duration_ms": 184000,
  "size": 5888044,
  "attempts": 0,
  "telegram_message_id": 0
}
```

Rules:
- Network activity must never stall an active recording
- Upload starts only after a recording fully finishes
- One file at a time
- `sent` only after Telegram responds `ok: true`, and only then store `telegram_message_id`
- On boot, any `uploading` record reverts to `pending` (crash recovery)
- Never delete unsent files automatically

## Telegram

- Bot API, `sendDocument`, multipart/form-data, stream file from SD (don't buffer whole file in RAM)
- Channel targeting: numeric `chat_id` or `@username`; one active channel, optional send-to-all
- Caption per upload for de-dup:
  ```
  Recorder ID: REC_20260804_215700_001
  Duration: 03:04
  Device: VoiceRecorder
  ```

## Wi-Fi

Multiple saved networks tried in order; must run fully offline; auto-reconnect; on reconnect,
kick the upload queue; show status on screen.

## Config (`/echo-pocket/config/recorder.ini`, plain text on SD)

```ini
[device]
name=VoiceRecorder
timezone=Europe/Berlin

[wifi_1]
ssid=HomeWiFi
password=secret_password

[telegram]
bot_token=1234567890:AAExampleToken
send_to_all=false
active_channel=1
channel_1_id=-1001234567890
channel_1_name=Voice Notes

[recorder]
auto_upload=true
delete_after_upload=false
sample_rate=16000
noise_suppression=true
voice_detection=true

[face]
theme=owl
react_to_voice=true
eye_min_size=5
eye_max_size=22
blink=true
animation_fps=20
```

Bad/missing config must never crash the firmware: SD recording keeps working, Telegram disables
itself, Wi-Fi may be unavailable, screen shows a clear error. No encryption of config in v1.0.

## UI

Minimalist, low frame rate (must not compete with audio capture for CPU/time).

- **Home screen**: face, ready status, Wi-Fi, SD, pending-upload count, battery (if available)
- **Recording screen**: face, REC indicator, timer only — no waveform/level meter by default
- **Main menu**: New Recording / Recordings / Unsent / Send All / Face / Telegram /
  Settings / Info (Wi-Fi status/IP, SD status, and battery % now live in Info)

### Face engine (plugin interface, built into firmware — no code loaded from SD)

```cpp
enum class FaceEvent {
    Idle, Recording, VoiceActive, Silence, Saving,
    Uploading, UploadSuccess, UploadError, LowBattery
};

class FacePlugin {
public:
    virtual ~FacePlugin() = default;
    virtual const char* id() const = 0;
    virtual const char* displayName() const = 0;
    virtual void begin() = 0;
    virtual void setEvent(FaceEvent event) = 0;
    virtual void update(float voiceLevel, bool voiceDetected, uint32_t deltaMs) = 0;
    virtual void draw() = 0;
};
```

- v1.0 built-in themes: **Owl, Minimal, Robot, Pixel, Vector** (compiled in, no plugin loading from SD)
- Themes only receive device state + voice level — never touch recording/network/SD directly
- A crashing/misbehaving theme must not corrupt the queue or recording
- Theme switch is live, no reboot; active theme persisted in config; unknown theme → falls back
  to `minimal`
- Eye size reacts to post-noise-suppression voice level, not raw mic level

## Buttons

- **Left**: back / cancel / previous item
- **Center**: select / start recording / stop recording / confirm
- **Right**: next item / open menu / open queue / secondary action
- v1.0 is short-press only (no long-press/double-press yet)

## Battery

Board has a single-cell 3.7V connector + charge/discharge circuit — **VBAT-to-ADC exposure is
unconfirmed for this revision**. Before implementing percentage display, verify against the
board schematic/official example whether: VBAT is exposed via a resistor divider to an ESP32-S3
ADC pin, charger status is readable, a separate fuel gauge exists, and the ADC pin isn't already
claimed by another peripheral.

If VBAT is not readable: show only `USB / Charging / Battery / Level unknown` — do not estimate
percentage from indirect signals.

If VBAT is readable via ADC:
- use ESP-IDF calibrated ADC, average multiple samples, account for divider ratio
- update slowly (every 10–30s), non-linear discharge curve (use a single-cell Li-ion table)
- discount Wi-Fi-induced voltage sag

Thresholds: >20% normal, ≤20% warning, ≤10% block auto-upload of large files, critical → safely
finish current recording (see lifecycle above) then power down if supported.

## Implementation status

v1.0 firmware is complete per the implementation plan at
`docs/plans/completed/20260804-esp32s3-voice-recorder-v1.md`. All 20 tasks are done;
on-device hardware verification is pending physical board availability (see
Post-Completion section in the plan).

### Architectural deviations from initial spec

1. **SD card uses SDMMC, not SPI**: The Waveshare board exposes SD over SDMMC 4-bit,
   so there is no SPI bus sharing with the LCD. The plan's "arbitration" concern is moot.
2. **Extra ring buffer stage**: The audio pipeline has a PSRAM ring buffer *between*
   capture and the AFE (not after it), plus a separate mono ring buffer between the AFE
   and the writer. This was the plan-revision fix for the original topology error.
3. **`device_events.h` lives in `components/board/`**: Moved out of `components/ui/` to
   break a `ui ↔ recorder` circular component dependency (board is the neutral home).
4. **Face engine is a `ui` subdirectory**: The plan initially described it as a separate
   component; it lives under `components/ui/face_engine/` to avoid an unregistered
   nested-component directory in ESP-IDF's build system.
5. **PSRAM is octal (ESP32-S3R8)**: Not quad — confirmed from vendor example
   `sdkconfig.defaults`. Getting this wrong would be a boot hang, not a compile error.
6. **VBAT is readable**: Confirmed via vendor `bsp_power_manager.c` — GPIO 1
   (ADC1_CH0) with a 200K/100K resistor divider (ratio 3.0, not 1:1 — on-device log
   showed 2766mV/0% against a near-full battery with the wrong 2.0 ratio; fixed in
   `battery.h`). `[recorder].sample_rate` is parsed but inert (hard-coded to 16000) —
   documented in the plan's Task 20 config audit.
7. **Internal/DMA-capable RAM is the binding memory constraint, not total PSRAM**:
   this board's internal SRAM is only ~170KB total, and ESP-IDF's
   `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` (80KB) reserves part of that
   specifically for DMA/internal-only allocations. FreeRTOS task stacks are
   *forced* into internal memory and draw from that same reserved pool (more/
   bigger tasks directly shrink SD-card/DMA headroom, not just general heap).
   On-device captures showed only single-digit KB free at boot — enough to
   intermittently fail SD card reads/writes and even Wi-Fi's WPA2 handshake
   crypto allocations. Knobs already tuned in `sdkconfig.defaults` to give this
   headroom: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` lowered to 1024 (so most
   small allocations default to PSRAM instead of internal),
   `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` (TLS buffers off internal RAM), and
   `app_main()` returning instead of idling forever (reclaims its 8KB stack).
   Any future feature adding tasks, TLS connections, or internal-RAM buffers
   should budget against this ~170KB internal pool, not total PSRAM.
8. **FatFs `rename()` does not overwrite an existing destination**, unlike
   POSIX `rename()` (which atomically replaces it). Any atomic-write pattern
   here — write to `<path>.tmp`, then `rename()` over the real file, used by
   `config_save()` and `queue_store` — must `remove(path)` before the
   `rename()` call, or every save after the very first one silently fails
   with `FR_EXIST`.

## v1.0 done-criteria (checklist)

- Buttons start/stop recording; WAV on SD is valid; 10-min recording has no dropouts
- Noise suppression measurably improves intelligibility
- Face eyes react to cleaned voice level; ≥4 built-in themes; live theme switch; unknown theme →
  minimal
- Works fully offline; each recording has a durable state; queue drains when Wi-Fi returns;
  `telegram_message_id` stored after confirmed send; reboot never loses the queue
- Network/screen never interrupt an active recording
- Config fully driven by plain-text INI on SD
- Battery shown when hardware supports it; honestly "unknown" when it doesn't; critical battery
  never corrupts the in-progress WAV

## Explicitly out of scope for v1.0

Speech recognition/transcription, Opus/Telegram voice messages, speaker selection among multiple
speakers, complex beamforming, cloud noise suppression, touch control, mobile app, loading
executable plugins/code from SD, config encryption, OTA firmware updates.
