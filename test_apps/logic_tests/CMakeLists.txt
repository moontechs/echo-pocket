cmake_minimum_required(VERSION 3.16)

# This is a standalone ESP-IDF project that links the main project's
# components/ via EXTRA_COMPONENT_DIRS and runs Unity tests over serial.
#
# Build & run:
#   idf.py -C test_apps/logic_tests -B build.esp32s3 build flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Pull in the Telegram component; other pure-logic sources are compiled directly.
set(EXTRA_COMPONENT_DIRS
    # components/ui: face_engine sources are compiled directly in main/CMakeLists.txt
    # (the full ui component requires esp_driver_spi + esp_lcd which aren't available here)
    # components/audio: audio_ringbuf.c is compiled directly in main/CMakeLists.txt
    # (the full audio component requires esp_codec_dev which isn't available here)
    # components/recorder: pure-logic files are compiled directly in main/CMakeLists.txt
    # TODO(task-14): add components/network after wifi_manager.c exists
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/telegram
)

project(logic_tests)
