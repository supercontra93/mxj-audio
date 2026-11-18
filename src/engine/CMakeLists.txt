# mxj_engine: real-time engine (playback, recording, tracks, mixer, devices).
add_library(mxj_engine STATIC
    devices/AudioEngine.cpp
    devices/AudioEngine.h
    mix/SessionMixer.cpp
    mix/SessionMixer.h
    playback/PlaybackSource.h
    playback/PlaybackEngine.cpp
    playback/PlaybackEngine.h
    recording/Recorder.cpp
    recording/Recorder.h)

target_link_libraries(mxj_engine PUBLIC mxj_formats)
mxj_configure_target(mxj_engine)
