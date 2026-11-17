# mxj_formats: reading and writing audio files and metadata.
add_library(mxj_formats STATIC
    import/AudioFileFormats.cpp
    import/AudioFileFormats.h
    import/AudioImporter.cpp
    import/AudioImporter.h
    import/FileSampleSource.cpp
    import/FileSampleSource.h)

target_link_libraries(mxj_formats PUBLIC mxj_dsp)
mxj_configure_target(mxj_formats)
