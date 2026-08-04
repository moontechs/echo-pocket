cmake_minimum_required(VERSION 3.16)

# This is a standalone ESP-IDF project that links the main project's
# components/ via EXTRA_COMPONENT_DIRS and runs Unity tests over serial.
#
# Build & run:
#   idf.py -C test_apps/logic_tests build flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Pull in the main project's pure-logic components.
# Components added as they are created; only 'board' exists at Task 1.
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/board
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/storage
    # TODO(task-5): add components/ui after face_engine exists
    # components/audio: audio_ringbuf.c is compiled directly in main/CMakeLists.txt
    # (the full audio component requires esp_codec_dev which isn't available here)
    # components/recorder: pure-logic files are compiled directly in main/CMakeLists.txt
    # TODO(task-14): add components/network after wifi_manager.c exists
    # TODO(task-16): add components/telegram after telegram_client.c exists
)

project(logic_tests)
