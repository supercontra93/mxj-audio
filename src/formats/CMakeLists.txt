# mxj_formats: reading and writing audio files and metadata. Filled in phase 1.
add_library(mxj_formats INTERFACE)
target_link_libraries(mxj_formats INTERFACE mxj_dsp)
