cmake_minimum_required(VERSION 3.16)

# This is a standalone ESP-IDF project that compiles the main project's
# pure-logic sources directly and runs Unity tests.
#
# Build & run:
#   idf.py -C test_apps/logic_tests -B test_apps/logic_tests/build.esp32s3 set-target esp32s3 build flash monitor

# Keep each build directory self-contained: ESP-IDF otherwise writes sdkconfig
# into the shared project root, making a Linux configure contaminate ESP32-S3.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

# This test project only needs its main component and the dependencies it
# declares.  Do not build every IDF component for the host target.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP-IDF's Linux port calls getentropy() via unistd.h, while macOS declares
# it only from sys/random.h. Use a global forced include because ESP-IDF
# creates its component directories independently of this project's directory.
if(APPLE AND IDF_TARGET STREQUAL "linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include sys/random.h")
endif()

project(logic_tests)
