# mxj_dsp: effects and analysis, independent of the user interface.
add_library(mxj_dsp STATIC
    analysis/LevelMeter.cpp
    analysis/LevelMeter.h
    analysis/Loudness.cpp
    analysis/Loudness.h
    analysis/Spectrum.cpp
    analysis/Spectrum.h
    convert/Resample.cpp
    convert/Resample.h
    effects/BuiltInEffects.h
    effects/DynamicsEffects.cpp
    effects/Effect.h
    effects/EffectCatalogue.cpp
    effects/EffectCatalogue.h
    effects/FilterEffects.cpp
    effects/Generators.cpp
    effects/OfflineEffects.cpp
    effects/Parameters.cpp
    effects/Parameters.h
    effects/SpaceEffects.cpp
    filters/Biquad.cpp
    filters/Biquad.h
    process/Analysis.cpp
    process/Analysis.h
    process/Render.cpp
    process/Render.h
    repair/NoiseReduction.cpp
    repair/NoiseReduction.h
    repair/Restoration.cpp
    repair/Restoration.h
    spectral/Fft.cpp
    spectral/Fft.h
    spectral/SpectralEdit.cpp
    spectral/SpectralEdit.h
    spectral/Stft.cpp
    spectral/Stft.h)

target_link_libraries(mxj_dsp PUBLIC mxj_core)
mxj_configure_target(mxj_dsp)
