# ESPresso — top-level CMakeLists.txt
# ESP-IDF project file. PlatformIO uses this to locate the main component.
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project boilerplate
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set EXTRA_COMPONENT_DIRS to find MicroLink in components/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

project(espresso)
