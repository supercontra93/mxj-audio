# Builds the JUCE modules we use exactly once, into the static library `mxj_juce`.
# Every MXJ layer links this library instead of linking juce:: modules directly;
# linking juce:: module targets into several libraries would compile them
# several times and cause duplicate symbols.

add_library(mxj_juce STATIC)

target_link_libraries(mxj_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(mxj_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_DONT_DECLARE_PROJECTINFO=1
        # ASIO: JUCE ships the ASIO SDK headers, which Steinberg offers under GPLv3.
        # GPLv3 code may be combined with our AGPLv3 program (AGPLv3 section 13).
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0)

# Re-export the include paths and definitions of the JUCE modules so that code
# linking mxj_juce can include <juce_core/juce_core.h> and friends.
target_include_directories(mxj_juce INTERFACE
    $<TARGET_PROPERTY:mxj_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(mxj_juce INTERFACE
    $<TARGET_PROPERTY:mxj_juce,COMPILE_DEFINITIONS>)

set_target_properties(mxj_juce PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "ThirdParty")
