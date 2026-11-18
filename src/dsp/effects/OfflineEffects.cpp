// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/BuiltInEffects.h"

/** Effects that need to see the whole selection before they can change it.
    They only declare their parameters here; dsp/process/Offline.cpp runs them. */
namespace mxj::dsp::builtin
{
    EffectType normalize()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "normalize";
        t.name = "Normalize (Peak)";
        t.category = "Amplitude and Dynamics";
        t.description = "Raise or lower the audio so its loudest peak reaches a level";
        t.kind = EffectType::Kind::offline;
        t.parameters = {
            P::number ("peak", "Peak level", -40.0f, 0.0f, -1.0f, "dB"),
            P::toggle ("separate", "Each channel on its own", false),
        };
        t.factoryPresets = { { "0 dB", { 0.0f, 0 } }, { "-1 dB", { -1.0f, 0 } }, { "-3 dB", { -3.0f, 0 } } };
        return t;
    }

    EffectType matchLoudness()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "matchLoudness";
        t.name = "Match Loudness (LUFS)";
        t.category = "Amplitude and Dynamics";
        t.description = "Bring the audio to a target loudness, such as -16 LUFS for podcasts";
        t.kind = EffectType::Kind::offline;
        t.parameters = {
            P::number ("target", "Target loudness", -36.0f, -6.0f, -16.0f, "LUFS"),
            P::number ("truePeak", "True peak limit", -9.0f, 0.0f, -1.0f, "dBTP"),
            P::toggle ("limit", "Use a limiter if peaks would pass the limit", true),
        };
        t.factoryPresets = {
            { "Podcast (-16 LUFS)",                 { -16, -1, 1 } },
            { "Podcast, mono (-19 LUFS)",           { -19, -1, 1 } },
            { "Spotify, YouTube (-14 LUFS)",        { -14, -1, 1 } },
            { "Apple Music (-16 LUFS)",             { -16, -1, 1 } },
            { "Broadcast EBU R128 (-23 LUFS)",      { -23, -1, 1 } },
            { "Audiobook (-20 LUFS)",               { -20, -3, 1 } },
        };
        return t;
    }

    EffectType speed()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "speed";
        t.name = "Change Speed";
        t.category = "Time and Pitch";
        t.description = "Play faster or slower; the pitch changes with the speed, like a tape";
        t.kind = EffectType::Kind::offline;
        t.parameters = { P::number ("speed", "Speed", 25.0f, 400.0f, 100.0f, "%", 1, 100.0f) };
        t.factoryPresets = { { "Half speed", { 50 } }, { "Double speed", { 200 } }, { "Slightly faster (+5%)", { 105 } } };
        return t;
    }
}
