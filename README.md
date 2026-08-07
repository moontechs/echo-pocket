# echo-pocket — ESP32-S3 Voice Recorder

Autonomous voice recorder firmware for the **Waveshare ESP32-S3-LCD-1.54** board (240×240 LCD,
no touch, dual mic + ES7210, microSD, 3 buttons, Wi-Fi/BLE). Records to WAV on microSD with
ESP-SR noise suppression, shows a voice-reactive animated face (5 swappable built-in themes),
uploads recordings to Telegram via a crash-safe persistent queue, and is fully configured from a
plain-text INI file on the SD card.

Full spec: [AGENTS.md](./AGENTS.md)

## Requirements

- **ESP-IDF** v5.x (see [Espressif setup guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html))
- Target chip: `esp32s3`
- Board: Waveshare ESP32-S3-LCD-1.54 (non-touch variant, ESP32-S3R8 with octal PSRAM)

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

The `sdkconfig.defaults` file provides all required Kconfig presets (PSRAM octal mode, 16MB
flash, partition table, etc.). No manual `menuconfig` is needed for a first build.

## Configuration

Create `/echo-pocket/config/recorder.ini` on a FAT32-formatted microSD card. On boot the
firmware creates the directory tree and reads the config. Example:

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

Bad/missing config never crashes the firmware — SD recording keeps working regardless.

## Running tests

Unit tests (pure logic, no hardware needed) live in `test_apps/logic_tests/`:

```bash
idf.py -C test_apps/logic_tests -B build.esp32s3 build flash monitor
```

For host-native tests without a board, build the Linux target and run the
resulting binary directly:

```bash
idf.py -C test_apps/logic_tests -B build.linux --preview set-target linux build
build.linux/logic_tests.elf
```

This builds a separate ESP-IDF project that links the main project's `components/` via
`EXTRA_COMPONENT_DIRS` and runs Unity test cases over serial. All tests must pass.

## Project structure

```
main/                       # app_main.c — boot sequence wiring
components/
  board/                    # Pin defs, board_init, buttons, battery, device_events.h
  audio/                    # I2S capture, PSRAM ring buffer, ESP-SR AFE, voice level
  recorder/                 # WAV writer, recording state machine, rec_id, split logic
  ui/
    face_engine/            # FacePlugin interface + registry + 5 built-in themes
    screens/                # Home, recording, menu, list screens
  storage/                  # SD mount, config parser + write-back, queue persistence
  network/                  # Wi-Fi manager, net_selection, upload task
  telegram/                 # Bot API client (sendDocument), caption formatting
test_apps/
  logic_tests/              # Unity-based unit tests (pure logic, no hardware)
```

## FreeRTOS task priority order

```
audio_capture_task     — highest (never blocks on SD/display/network)
audio_process_task     — high
sd_writer_task         — high
ui_task                — normal
wifi_task / upload_task — low
```

The priority ordering enforces the "capture never blocks" guarantee.
