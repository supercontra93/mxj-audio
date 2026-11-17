# MXJ AUDIO - free and open source audio editor
# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)

project(MXJAudio
    VERSION 0.2.0
    DESCRIPTION "Free and open source audio editor"
    HOMEPAGE_URL "https://github.com/supercontra93/mxj-audio"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(MXJ_BUILD_TESTS "Build unit tests" ON)

# Development builds must never contact the update server.
# Phase 8 turns this on for official release builds only.
option(MXJ_ENABLE_UPDATES "Enable the in-app update check" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MxjDependencies)
include(MxjJuce)
include(MxjCompilerSettings)

# Layers, bottom to top. A layer may only use the layers listed before it.
# tools/check_layers.py enforces this in CI.
add_subdirectory(src/core)
add_subdirectory(src/dsp)
add_subdirectory(src/formats)
add_subdirectory(src/engine)
add_subdirectory(src/plugins)
add_subdirectory(src/services)
add_subdirectory(src/app)

if(MXJ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
