add_executable(mxj_tests
    AudioModelTests.cpp
    EffectTests.cpp
    EditTests.cpp
    ExportTests.cpp
    RecorderTests.cpp
    CommandRegistryTests.cpp
    ImportTests.cpp
    PlaybackTests.cpp
    PluginTests.cpp
    RepairTests.cpp
    SessionTests.cpp
    HistoryTests.cpp
    SettingsTests.cpp
    LogTests.cpp
    TranslationTests.cpp)

target_link_libraries(mxj_tests PRIVATE mxj_services Catch2::Catch2WithMain)

# Test plugins (phase 6): a CLAP gain, a CLAP that crashes when opened, and a VST3 gain made with JUCE.
# Each in a folder of its own, so a scan of the "good" folder does not meet the crashing one.
foreach(name TestGainClap TestCrashClap)
    add_library(${name} MODULE plugins/${name}.cpp)
    target_link_libraries(${name} PRIVATE mxj_clap)
    target_compile_features(${name} PRIVATE cxx_std_20)
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".clap" FOLDER "Tests"
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/clap-plugins/${name}/$<CONFIG>")
endforeach()

juce_add_plugin(mxj_test_gain
    FORMATS VST3
    PRODUCT_NAME "MXJ Test Gain"
    COMPANY_NAME "MXJ AUDIO"
    PLUGIN_MANUFACTURER_CODE Mxja
    PLUGIN_CODE Mxtg
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(mxj_test_gain PRIVATE plugins/TestGainVst3.cpp)
target_compile_definitions(mxj_test_gain PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                                JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(mxj_test_gain PRIVATE juce::juce_audio_processors juce::juce_audio_utils
                                    PUBLIC juce::juce_recommended_config_flags)
set_target_properties(mxj_test_gain mxj_test_gain_VST3 PROPERTIES FOLDER "Tests")

add_dependencies(mxj_tests TestGainClap TestCrashClap mxj_test_gain_VST3 mxj_plugin_scanner)
target_compile_definitions(mxj_tests PRIVATE
    MXJ_TRANSLATIONS_DIR="${PROJECT_SOURCE_DIR}/resources/i18n"
    MXJ_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src"
    MXJ_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    MXJ_TEST_CLAP_GAIN="$<TARGET_FILE:TestGainClap>"
    MXJ_TEST_CLAP_CRASH="$<TARGET_FILE:TestCrashClap>"
    MXJ_TEST_VST3_BINARY="$<TARGET_FILE:mxj_test_gain_VST3>"
    MXJ_TEST_SCANNER="$<TARGET_FILE:mxj_plugin_scanner>"
    MXJ_LUA_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/resources/lua-plugins")
mxj_configure_target(mxj_tests)

include(Catch)
catch_discover_tests(mxj_tests DISCOVERY_MODE PRE_TEST)
