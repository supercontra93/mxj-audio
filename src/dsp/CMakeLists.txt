# mxj_dsp: effects and analysis, independent of the user interface.
# Phase 2: offline sample rate conversion. Effects arrive in phase 3.
add_library(mxj_dsp STATIC
    convert/Resample.cpp
    convert/Resample.h)

target_link_libraries(mxj_dsp PUBLIC mxj_core)
mxj_configure_target(mxj_dsp)
