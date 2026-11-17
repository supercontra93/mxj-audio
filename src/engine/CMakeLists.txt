# mxj_engine: real-time engine (playback, recording, tracks, mixer, devices). Filled in phase 1.
add_library(mxj_engine INTERFACE)
target_link_libraries(mxj_engine INTERFACE mxj_formats)
