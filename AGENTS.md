# AGENTS.md — ESP32-S3 Voice Recorder (echo-pocket)

Autonomous voice recorder firmware for the **Waveshare ESP32-S3-LCD-1.54** board (no touch).
Records voice to microSD with AI noise suppression, shows an animated reactive face on a 240×240
LCD, controlled by 3 buttons, auto-uploads recordings to Telegram with a persistent retry queue.
All config (Wi-Fi, Telegram, channels) lives in a plain-text INI file on the SD card.

Full requirements: this file. Source spec was written in Russian by the project owner; keep
terminology below in sync with it if the spec changes.

## Hardware

- ESP32-S3, PSRAM + built-in flash
- 1.54" LCD, 240×240, no touch
- 2× microphones → ES7210 audio codec → I²S
- microSD / TF card
- Wi-Fi 2.4GHz, BLE 5
- 3 physical buttons (left / center / right)
- Single-cell 3.7V Li-ion/LiPo, onboard charge circuit (VBAT-to-ADC exposure not confirmed —
  verify against board schematic/example before building the battery gauge, see §Battery)

## Toolchain

- **ESP-IDF** (preferred over Arduino framework)
- Component layout under `components/`, app entry in `main/`

```
main/
components/
  board/          # pin defs, board bring-up
  audio/          # I2S capture, ES7210, ESP-SR pipeline
  recorder/       # WAV writer, recording state machine
  ui/
    face_engine/  # FacePlugin interface + registry + built-in themes
  storage/        # SD mount, config parsing, queue persistence
  network/        # wifi manager
  telegram/       # bot API client (sendDocument)
```

FreeRTOS tasks: `audio_capture_task`, `audio_process_task`, `sd_writer_task`, `ui_task`,
`wifi_task`, `upload_task`. Audio capture must never block on SD, display, or network — use a
PSRAM ring buffer between audio capture and the writer.

## Audio pipeline

```
2× mic → ES7210 → I2S → ESP-SR AFE (noise suppression, VAD, AGC) → mono PCM → WAV on SD
```

- Format: WAV, PCM s16, mono, 16kHz (~1.92 MB/min)
- Use Espressif **ESP-SR** for noise suppression in v1.0 (built-in, not neural-from-scratch)
- Enable: noise suppression, VAD, moderate AGC
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
- **Main menu**: New Recording / Recordings / Unsent / Send All / Face / Wi-Fi / Telegram /
  Settings / Info

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

- v1.0 built-in themes: **Owl, Minimal, Robot, Pixel** (compiled in, no plugin loading from SD)
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

## Dev stages (build in this order)

1. Board bring-up: LCD, 3 buttons, SD, mics, I²S test
2. Raw dictaphone: WAV record, start/stop, correct header, 10-min no-dropout test
3. Noise suppression: ESP-SR + VAD + AGC, raw vs clean comparison
4. Minimal UI: face engine + registry + 4 themes, theme switch via menu, voice-reactive eyes,
   blink, timer, Wi-Fi/SD/battery/queue status
5. Telegram: Wi-Fi connect, `getMe` test, small WAV send, streamed large WAV send
6. Robust queue: pending/uploading/sent states, restart recovery, manual "Send All"

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
