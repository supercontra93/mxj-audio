add_executable(mxj_tests
    CommandRegistryTests.cpp
    HistoryTests.cpp
    SettingsTests.cpp
    LogTests.cpp
    TranslationTests.cpp)

target_link_libraries(mxj_tests PRIVATE mxj_services Catch2::Catch2WithMain)
target_compile_definitions(mxj_tests PRIVATE
    MXJ_TRANSLATIONS_DIR="${PROJECT_SOURCE_DIR}/resources/i18n"
    MXJ_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src")
mxj_configure_target(mxj_tests)

include(Catch)
catch_discover_tests(mxj_tests DISCOVERY_MODE PRE_TEST)
