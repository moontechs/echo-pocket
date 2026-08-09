# echo-pocket

**Autonomous voice recorder firmware for the ESP32-S3.** Press a button, it records to
microSD with AI noise suppression, shows a voice-reactive animated face, and ships the
recording to Telegram on its own — no phone app, no cloud dashboard, no companion service.

Built for the **Waveshare ESP32-S3-LCD-1.54** board (240×240 LCD, no touch, dual mic +
ES7210 codec, microSD, 3 buttons, Wi-Fi/BLE).

Full spec: [AGENTS.md](./AGENTS.md)

## Features

- **Local-first recording** — WAV to microSD, works with no Wi-Fi and no config at all
- **On-device noise suppression & voice detection** via ESP-SR AFE
- **Crash-safe upload queue** — recordings auto-send to Telegram, survive reboots/failures,
  retry without duplicating or losing anything
- **Animated face UI** (Vector theme) that reacts to voice level in real time
- **Zero-app config** — Wi-Fi, Telegram, and UI settings live in a plain-text INI file on
  the SD card, editable from any computer
- **Battery-aware** — reads VBAT/charging state, blocks large uploads and safely powers
  down on critical battery, auto-shuts-down after 5 minutes idle

## Hardware

| Part           | Detail                                              |
|----------------|------------------------------------------------------|
| Board          | Waveshare ESP32-S3-LCD-1.54 (non-touch variant)      |
| MCU            | ESP32-S3R8 — octal PSRAM 8MB, 16MB flash             |
| Display        | 1.54" 240×240 LCD, ST7789 over SPI                   |
| Audio in       | 2× mic → ES7210 codec (I²C) → I²S                    |
| Storage        | microSD/TF over SDMMC 4-bit                          |
| Radio          | Wi-Fi 2.4GHz, BLE 5                                  |
| Input          | 3 buttons — left / center / right, active-low        |
| Power          | Single-cell 3.7V Li-ion/LiPo + onboard charge circuit|

Full pin map: [AGENTS.md § Hardware](./AGENTS.md#hardware)

## Requirements

- **ESP-IDF** v5.x (see [Espressif setup guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html))
- Target chip: `esp32s3`

## Quick start

```bash
# 1. Set up ESP-IDF environment (if not already done)
. $IDF_PATH/export.sh

# 2. Set target and build
idf.py set-target esp32s3
idf.py build

# 3. Flash and monitor
idf.py flash monitor
```

`sdkconfig.defaults` provides every required Kconfig preset (PSRAM octal mode, 16MB flash,
partition table, etc.) — no manual `menuconfig` needed for a first build.

## Configuration

Create `/echo-pocket/config/recorder.ini` on a FAT32-formatted microSD card. On boot the
firmware creates the directory tree and reads the config — a missing or malformed file
never crashes the firmware, it just falls back to safe defaults.

```ini
[device]
name=VoiceRecorder
# POSIX TZ string (not an IANA name — ESP-IDF has no zoneinfo database)
timezone=CET-1CEST,M3.5.0,M10.5.0/3

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
noise_suppression=true
voice_detection=true

[face]
theme=vector
```

## Controls

| Button | Action                                              |
|--------|------------------------------------------------------|
| Left   | Back / cancel / previous item                        |
| Center | Select / start recording / stop recording / confirm  |
| Right  | Next item / open menu / open queue / secondary action|

## Project structure

```
main/                       # app_main.c — boot sequence wiring
components/
  board/                    # Pin defs, board_init, buttons, battery, device_events.h
  audio/                    # I2S capture, PSRAM ring buffer, ESP-SR AFE, voice level
  recorder/                 # WAV writer, recording state machine, rec_id, split logic
  ui/
    face_engine/            # FacePlugin interface + registry + Vector theme
    screens/                # Home, recording, menu, list screens
  storage/                  # SD mount, config parser + write-back, queue persistence
  network/                  # Wi-Fi manager, net_selection, upload task
  telegram/                 # Bot API client (sendDocument), caption formatting
test_apps/
  logic_tests/              # Unity-based unit tests (pure logic, no hardware)
```

## Running tests

Unit tests (pure logic, no hardware needed) live in `test_apps/logic_tests/`:

```bash
idf.py -C test_apps/logic_tests -B test_apps/logic_tests/build.esp32s3 set-target esp32s3 build flash monitor
```

For host-native tests without a board, build the Linux target and run the resulting
binary directly:

```bash
idf.py -C test_apps/logic_tests -B test_apps/logic_tests/build.linux --preview set-target linux build
test_apps/logic_tests/build.linux/logic_tests.elf
```

This builds a separate ESP-IDF project from directly compiled pure-logic sources. The
ESP32-S3 target runs over serial; the Linux target runs locally. Each build directory
keeps its own generated `sdkconfig`, so the two commands can be used interchangeably.
All tests must pass.

## Contributing

Issues and PRs welcome. Read [AGENTS.md](./AGENTS.md) first — it's the full spec
(architecture, audio pipeline, config format, UI/face engine, upload queue) and keeps this
README from having to repeat it.

## License

No license file yet — treat this repo as all-rights-reserved until one is added.
