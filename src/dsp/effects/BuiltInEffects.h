// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Effect.h"

/** The built-in effects of MXJ AUDIO (master plan, module E), one function per
    effect type. EffectCatalogue collects them. */
namespace mxj::dsp::builtin
{
    // Amplitude and dynamics (DynamicsEffects.cpp)
    EffectType amplify();
    EffectType compressor();
    EffectType limiter();
    EffectType gate();
    EffectType deEsser();

    // Filter and EQ (FilterEffects.cpp)
    EffectType parametricEq();
    EffectType graphicEq();
    EffectType filter();

    // Delay, reverb and modulation (SpaceEffects.cpp)
    EffectType delay();
    EffectType reverb();
    EffectType chorus();
    EffectType flanger();
    EffectType phaser();

    // Stereo (SpaceEffects.cpp)
    EffectType stereoTools();

    // Work on the whole selection (OfflineEffects.cpp); run by dsp/process
    EffectType normalize();
    EffectType matchLoudness();
    EffectType speed();

    // Noise reduction and repair (phase 4: repair/NoiseReduction.cpp, repair/Restoration.cpp)
    EffectType noiseReduction();
    EffectType adaptiveNoiseReduction();
    EffectType humRemoval();
    EffectType clickRemoval();
    EffectType clipRepair();

    // Generators (Generators.cpp)
    EffectType toneGenerator();
    EffectType noiseGenerator();
}
