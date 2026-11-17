# mxj_core: audio data model, project model, commands, undo history.
# Lowest layer: may only use JUCE's non-GUI modules and the C++ standard library.
configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h" @ONLY)

add_library(mxj_core STATIC
    audio/AudioSequence.cpp
    audio/AudioSequence.h
    audio/BlockStore.cpp
    audio/BlockStore.h
    audio/PeakCache.cpp
    audio/PeakCache.h
    audio/SampleSource.cpp
    audio/SampleSource.h
    commands/CommandRegistry.cpp
    commands/CommandRegistry.h
    commands/CommandInfo.h
    history/History.cpp
    history/History.h
    Version.h.in)

target_include_directories(mxj_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(mxj_core PUBLIC mxj_juce)
mxj_configure_target(mxj_core)
