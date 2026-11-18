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

# Lua 5.4 - MIT. Runs MXJ plugins (phase 6). See docs/adr/0010.
# Compiled as C++ so that Lua errors unwind C++ objects (exceptions instead of longjmp).
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.8
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lua)

set(MXJ_LUA_SOURCES lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc
    linit liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib
    ltable ltablib ltm lundump lutf8lib lvm lzio)
list(TRANSFORM MXJ_LUA_SOURCES PREPEND "${lua_SOURCE_DIR}/")
list(TRANSFORM MXJ_LUA_SOURCES APPEND ".c")
add_library(mxj_lua STATIC ${MXJ_LUA_SOURCES})
set_source_files_properties(${MXJ_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
target_include_directories(mxj_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
target_compile_features(mxj_lua PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mxj_lua PRIVATE /W0 /TP /EHsc)
else()
    target_compile_options(mxj_lua PRIVATE -w)
endif()
set_target_properties(mxj_lua PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "ThirdParty")

# CLAP - MIT. Header-only plugin interface, used to host CLAP plugins (phase 6). See docs/adr/0010.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-build)   # headers only: do not add its CMake project
FetchContent_MakeAvailable(clap)
add_library(mxj_clap INTERFACE)
target_include_directories(mxj_clap SYSTEM INTERFACE "${clap_SOURCE_DIR}/include")
