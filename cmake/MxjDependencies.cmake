# Third-party dependencies, downloaded at configure time.
# Every entry here must also be listed in THIRD_PARTY.md with its verified licence.
# Adding a dependency requires a decision record in docs/adr/.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# JUCE 9 - AGPL-3.0-only (we use the free AGPLv3 option). See docs/adr/0001.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

if(MXJ_BUILD_TESTS)
    # Catch2 3 - BSL-1.0. Used only by the test programs, never shipped.
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()
