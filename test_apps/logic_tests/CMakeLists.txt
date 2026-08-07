cmake_minimum_required(VERSION 3.16)

# This is a standalone ESP-IDF project that compiles the main project's
# pure-logic sources directly and runs Unity tests.
#
# Build & run:
#   idf.py -C test_apps/logic_tests -B test_apps/logic_tests/build.esp32s3 build flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(logic_tests)
