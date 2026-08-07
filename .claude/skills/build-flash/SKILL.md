---
name: build-flash
description: Build the echo-pocket ESP32-S3 firmware with ESP-IDF and flash it to the connected board. Use when the user asks to build, flash, or run the firmware on hardware.
---

# Build & flash echo-pocket

ESP-IDF v5.3 lives at `~/esp/esp-idf` on this machine (installed for target
`esp32s3`). It is NOT on PATH by default — every new shell must source its
env script first.

## 1. Source the environment (every new shell)

```bash
source ~/esp/esp-idf/export.sh
```

## 2. Build

```bash
cd /Users/michael/github.com/moontechs/echo-pocket
idf.py build
```

First build after a fresh IDF checkout takes several minutes (compiles all
ESP-IDF component libs). Incremental builds are fast.

If CMake fails with `Could not create symbolic link ... File exists`, the
`build/` dir has stale symlinks from a previous/different IDF install path.
Fix: `rm -rf build && idf.py build` (safe — `build/` is gitignored output).

## 3. Flash

Board is a Waveshare ESP32-S3-LCD-1.54, connects as a USB serial port, e.g.
`/dev/cu.usbmodem11101` (port suffix number can change between plug-ins —
re-check with `ls /dev/cu.usbmodem*` if flash can't find the device).

```bash
idf.py -p /dev/cu.usbmodem11101 flash
```

Omit `-p` to let esptool auto-detect, but auto-detect can pick the wrong
port if other USB-serial devices are attached — passing it explicitly is
more reliable.

**If flashing fails with "No serial data received":** the board isn't in
bootloader/download mode. Hold **BOOT**, tap **RESET** while still holding
BOOT, then release BOOT, then retry the flash command. This board does not
reliably auto-reset into download mode over DTR/RTS.

## 4. Monitor (optional)

```bash
idf.py -p /dev/cu.usbmodem11101 monitor
```
Exit with `Ctrl+]`. Combine steps: `idf.py -p /dev/cu.usbmodem11101 flash monitor`.

## One-liner for a fresh shell

```bash
source ~/esp/esp-idf/export.sh && cd /Users/michael/github.com/moontechs/echo-pocket && idf.py build && idf.py -p /dev/cu.usbmodem11101 flash
```

## Logic tests (host-independent, still needs hardware/IDF)

```bash
source ~/esp/esp-idf/export.sh
idf.py -C test_apps/logic_tests -p /dev/cu.usbmodem11101 build flash monitor
```
