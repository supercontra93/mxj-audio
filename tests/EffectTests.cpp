// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "core/edit/EditOperations.h"
#include "dsp/analysis/Loudness.h"
#include "dsp/analysis/Spectrum.h"
#include "dsp/effects/EffectCatalogue.h"
#include "dsp/filters/Biquad.h"
#include "dsp/process/Analysis.h"
#include "engine/playback/PlaybackEngine.h"
#include "services/presets/PresetStore.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace mxj;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    juce::AudioBuffer<float> sine (int channels, int frames, double hz, float amplitude, double phase = 0.0)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, amplitude * (float) std::sin (twoPi * hz * i / rate + phase));

        return b;
    }

    juce::AudioBuffer<float> noise (int channels, int frames, float amplitude, int seed = 1)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, amplitude * (r.nextFloat() * 2.0f - 1.0f));

        return b;
    }

    AudioSequence toSequence (juce::AudioBuffer<float> b)
    {
        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate));
    }

    juce::AudioBuffer<float> readAll (const AudioSequence& s)
    {
        juce::AudioBuffer<float> b (s.getNumChannels(), (int) s.getNumFrames());
        s.read (b.getArrayOfWritePointers(), b.getNumChannels(), 0, b.getNumSamples());
        return b;
    }

    float peakDb (const juce::AudioBuffer<float>& b, int from = 0)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            p = juce::jmax (p, b.getMagnitude (c, from, b.getNumSamples() - from));
        return dsp::gainToDb (p);
    }

    /** Runs one effect over a buffer, in blocks, as the audio thread would. */
    void runEffect (const juce::String& id, std::vector<std::pair<juce::String, float>> changes, juce::AudioBuffer<float>& b,
                    int blockSize = 480)
    {
        const auto* type = dsp::catalogue::find (id);
        REQUIRE (type != nullptr);
        auto values = type->createValues();
        for (const auto& [param, v] : changes)
        {
            const auto index = type->indexOf (param);
            REQUIRE (index >= 0);
            values->set (index, v);
        }

        auto p = type->create (values);
        p->prepare (rate, blockSize, b.getNumChannels());

        for (int start = 0; start < b.getNumSamples(); start += blockSize)
        {
            const auto n = juce::jmin (blockSize, b.getNumSamples() - start);
            float* ptrs[8];
            for (int c = 0; c < b.getNumChannels(); ++c)
                ptrs[c] = b.getWritePointer (c, start);
            p->process (ptrs, b.getNumChannels(), n);
        }
    }

    float measureIntegrated (const juce::AudioBuffer<float>& b)
    {
        dsp::LoudnessMeter m;
        m.prepare (rate, b.getNumChannels());
        m.process (b.getArrayOfReadPointers(), b.getNumChannels(), b.getNumSamples());
        return m.getIntegrated();
    }

    juce::AudioBuffer<float> concat (std::initializer_list<juce::AudioBuffer<float>> parts)
    {
        int total = 0;
        for (const auto& p : parts)
            total += p.getNumSamples();

        juce::AudioBuffer<float> out (parts.begin()->getNumChannels(), total);
        int pos = 0;
        for (const auto& p : parts)
        {
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.copyFrom (c, pos, p, c, 0, p.getNumSamples());
            pos += p.getNumSamples();
        }
        return out;
    }
}

//==============================================================================
TEST_CASE ("Every effect type is complete and its presets fit its parameters", "[effects]")
{
    juce::StringArray ids;
    for (const auto& t : dsp::catalogue::all())
    {
        INFO (t.id);
        CHECK (t.id.isNotEmpty());
        CHECK (! ids.contains (t.id));
        ids.add (t.id);
        CHECK (t.name.isNotEmpty());
        CHECK (t.category.isNotEmpty());
        CHECK ((t.kind == dsp::EffectType::Kind::offline || t.create != nullptr));

        juce::StringArray paramIds;
        for (const auto& p : t.parameters)
        {
            CHECK (! paramIds.contains (p.id));
            paramIds.add (p.id);
            CHECK (p.limit (p.defaultValue) == p.defaultValue);
        }

        for (const auto& preset : t.factoryPresets)
        {
            INFO (preset.name);
            CHECK (preset.values.size() == t.parameters.size());
            for (size_t i = 0; i < juce::jmin (preset.values.size(), t.parameters.size()); ++i)
                CHECK (t.parameters[i].limit (preset.values[i]) == preset.values[i]);
        }
    }
}

TEST_CASE ("Every real-time effect turns noise into finite audio at any block size", "[effects]")
{
    for (const auto& t : dsp::catalogue::all())
    {
        if (t.create == nullptr)
            continue;

        INFO (t.id);
        for (int blockSize : { 1, 64, 480, 4096 })
        {
            auto b = noise (2, 24000, 0.5f);
            runEffect (t.id, {}, b, blockSize);

            bool finite = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    finite = finite && std::isfinite (b.getSample (c, i)) && std::abs (b.getSample (c, i)) < 8.0f;
            CHECK (finite);
        }

        // Presets too, and mono.
        for (const auto& preset : t.factoryPresets)
        {
            INFO (preset.name);
            auto values = t.createValues();
            values->setAll (preset.values);
            auto p = t.create (values);
            p->prepare (rate, 512, 1);
            auto b = noise (1, 9600, 0.5f);
            float* ptr[] { b.getWritePointer (0) };
            for (int s = 0; s < b.getNumSamples(); s += 512)
            {
                ptr[0] = b.getWritePointer (0, s);
                p->process (ptr, 1, juce::jmin (512, b.getNumSamples() - s));
            }
            CHECK (std::isfinite (b.getRMSLevel (0, 0, b.getNumSamples())));
        }
    }
}

TEST_CASE ("Effects at their default settings leave the audio alone where they should", "[effects]")
{
    for (const auto* id : { "amplify", "parametricEq", "graphicEq", "stereoTools" })
    {
        INFO (id);
        auto original = noise (2, 9600, 0.5f);
        auto b = original;
        runEffect (id, {}, b);

        float maxDiff = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (b.getSample (c, i) - original.getSample (c, i)));
        CHECK (maxDiff < 1.0e-5f);
    }
}

TEST_CASE ("Biquad designs have the expected response", "[effects][dsp]")
{
    using K = dsp::BiquadCoefficients;
    CHECK_THAT (20.0 * std::log10 (K::peak (rate, 1000, 1.0, 6.0).magnitude (rate, 1000)), WithinAbs (6.0, 0.01));
    CHECK_THAT (20.0 * std::log10 (K::lowPass (rate, 1000, 0.7071).magnitude (rate, 1000)), WithinAbs (-3.01, 0.05));
    CHECK_THAT (20.0 * std::log10 (K::highPass (rate, 1000, 0.7071).magnitude (rate, 1000)), WithinAbs (-3.01, 0.05));
    CHECK_THAT (20.0 * std::log10 (K::lowShelf (rate, 100, 0.707, -6.0).magnitude (rate, 20)), WithinAbs (-6.0, 0.2));
    CHECK_THAT (20.0 * std::log10 (K::highShelf (rate, 5000, 0.707, 4.0).magnitude (rate, 20000)), WithinAbs (4.0, 0.2));
    CHECK_THAT (K::allPass (rate, 1000, 0.7).magnitude (rate, 3000), WithinAbs (1.0, 1.0e-6));

    // The parametric EQ draws the curve it plays.
    const auto* eq = dsp::catalogue::find ("parametricEq");
    auto values = eq->getDefaults();
    values[(size_t) eq->indexOf ("b2Gain")] = 9.0f;
    CHECK_THAT (eq->response (values, rate, 1000.0), WithinAbs (9.0, 0.05));
    CHECK_THAT (eq->response (values, rate, 100.0), WithinAbs (0.0, 0.3));
}

TEST_CASE ("The filter removes what it should", "[effects]")
{
    auto low = sine (1, 48000, 100.0, 0.5f);
    auto high = sine (1, 48000, 5000.0, 0.5f);

    runEffect ("filter", { { "type", 0.0f }, { "freq", 1000.0f }, { "slope", 1.0f } }, low);   // high-pass
    runEffect ("filter", { { "type", 1.0f }, { "freq", 1000.0f }, { "slope", 1.0f } }, high);  // low-pass

    CHECK (peakDb (low, 24000) < -6.0f - 70.0f);   // 100 Hz, 3.3 octaves below, 24 dB/octave
    CHECK (peakDb (high, 24000) < -6.0f - 50.0f);
}

TEST_CASE ("The compressor turns loud parts down by its ratio", "[effects]")
{
    auto b = sine (2, 48000, 1000.0, 1.0f);
    runEffect ("compressor", { { "threshold", -20.0f }, { "ratio", 4.0f }, { "knee", 0.0f },
                               { "attack", 1.0f }, { "detection", 0.0f } }, b);

    // 0 dB in, 20 dB over the threshold, 4:1 -> 5 dB over: -15 dB.
    CHECK_THAT (peakDb (b, 24000), WithinAbs (-15.0, 0.6));

    auto quiet = sine (2, 48000, 1000.0, 0.01f); // -40 dB, below the threshold
    runEffect ("compressor", { { "threshold", -20.0f }, { "ratio", 4.0f }, { "knee", 0.0f } }, quiet);
    CHECK_THAT (peakDb (quiet, 24000), WithinAbs (-40.0, 0.1));
}

TEST_CASE ("The limiter never lets a sample pass the ceiling", "[effects]")
{
    auto b = noise (2, 96000, 1.0f);
    runEffect ("limiter", { { "inputGain", 12.0f }, { "ceiling", -3.0f } }, b);
    CHECK (peakDb (b) <= -3.0f + 0.001f);

    // Quiet audio passes unchanged, only delayed by the look-ahead.
    const auto* type = dsp::catalogue::find ("limiter");
    auto values = type->createValues();
    auto p = type->create (values);
    p->prepare (rate, 512, 1);
    const auto latency = p->getLatencyFrames();
    CHECK (latency == 239); // 5 ms at 48 kHz, minus one

    juce::AudioBuffer<float> impulse (1, 1024);
    impulse.clear();
    impulse.setSample (0, 10, 0.5f);
    float* ptr[] { impulse.getWritePointer (0) };
    p->process (ptr, 1, 1024);
    CHECK (impulse.getSample (0, 10 + latency) == 0.5f);
}

TEST_CASE ("Applying a look-ahead effect keeps the audio in place", "[effects][render]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    juce::AudioBuffer<float> b (1, 20000);
    b.clear();
    b.setSample (0, 5000, 0.25f);
    const auto input = toSequence (b);

    dsp::ChainSettings chain { dsp::EffectSettings::defaultsFor ("limiter"), dsp::EffectSettings::defaultsFor ("amplify") };
    chain[1].values[0] = 6.0f;
    auto processors = dsp::ProcessorChain::create (chain);
    const auto out = dsp::render (input, *processors, store);

    REQUIRE (out.getNumFrames() == input.getNumFrames());
    const auto result = readAll (out);
    CHECK_THAT (result.getSample (0, 5000), WithinAbs (0.25 * std::pow (10.0, 6.0 / 20.0), 1.0e-4));
    CHECK (result.getSample (0, 4999) == 0.0f);
}

TEST_CASE ("The noise gate quietens audio below the threshold", "[effects]")
{
    auto quiet = noise (1, 48000, 0.001f); // about -60 dB
    const auto before = quiet.getRMSLevel (0, 24000, 24000);
    runEffect ("gate", { { "threshold", -40.0f }, { "reduction", 30.0f } }, quiet);
    CHECK_THAT (dsp::gainToDb (quiet.getRMSLevel (0, 24000, 24000) / before), WithinAbs (-30.0, 1.0));

    auto loud = noise (1, 48000, 0.5f);
    const auto loudBefore = loud.getRMSLevel (0, 24000, 24000);
    runEffect ("gate", { { "threshold", -40.0f } }, loud);
    CHECK_THAT (loud.getRMSLevel (0, 24000, 24000), WithinAbs (loudBefore, 1.0e-4));
}

TEST_CASE ("The de-esser only turns down the high band", "[effects]")
{
    auto low = sine (1, 48000, 300.0, 0.5f);
    auto high = sine (1, 48000, 8000.0, 0.5f);
    runEffect ("deEsser", { { "frequency", 5000.0f }, { "threshold", -30.0f }, { "maxReduction", 10.0f } }, low);
    runEffect ("deEsser", { { "frequency", 5000.0f }, { "threshold", -30.0f }, { "maxReduction", 10.0f } }, high);

    CHECK_THAT (peakDb (low, 24000), WithinAbs (-6.02, 0.5));
    CHECK (peakDb (high, 24000) < -6.02f - 7.0f);
}

TEST_CASE ("Stereo tools swap, invert and narrow the channels", "[effects]")
{
    juce::AudioBuffer<float> b (2, 4);
    for (int i = 0; i < 4; ++i)
    {
        b.setSample (0, i, 0.5f);
        b.setSample (1, i, -0.25f);
    }

    auto swapped = b;
    runEffect ("stereoTools", { { "swap", 1.0f } }, swapped);
    CHECK (swapped.getSample (0, 0) == -0.25f);
    CHECK (swapped.getSample (1, 0) == 0.5f);

    auto mono = b;
    runEffect ("stereoTools", { { "mono", 1.0f } }, mono);
    CHECK (mono.getSample (0, 0) == 0.125f);
    CHECK (mono.getSample (1, 0) == 0.125f);

    auto inverted = b;
    runEffect ("stereoTools", { { "invertLeft", 1.0f } }, inverted);
    CHECK (inverted.getSample (0, 0) == -0.5f);
    CHECK (inverted.getSample (1, 0) == -0.25f);
}

TEST_CASE ("Generators make tones and noise of the requested level and length", "[effects][render]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    auto tone = dsp::EffectSettings::defaultsFor ("tone");
    tone.values[1] = 1000.0f;
    tone.values[2] = -12.0f;
    const auto out = dsp::generate (tone, 2, rate, 48000, store);
    REQUIRE (out.getNumFrames() == 48000);
    REQUIRE (out.getNumChannels() == 2);
    CHECK_THAT (peakDb (readAll (out)), WithinAbs (-12.0, 0.05));

    auto pink = dsp::EffectSettings::defaultsFor ("noise");
    const auto n = dsp::generate (pink, 1, rate, 24000, store);
    REQUIRE (n.getNumFrames() == 24000);
    CHECK (peakDb (readAll (n)) <= -18.0f + 0.01f);
    CHECK (peakDb (readAll (n)) > -40.0f);
}

TEST_CASE ("Reverse, channel gains and speed change render correctly", "[effects][render]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    juce::AudioBuffer<float> b (2, 30000);
    for (int i = 0; i < 30000; ++i)
    {
        b.setSample (0, i, (float) i / 30000.0f);
        b.setSample (1, i, -0.5f);
    }
    const auto input = toSequence (b);

    const auto reversed = readAll (dsp::reverse (input, store));
    CHECK (reversed.getSample (0, 0) == b.getSample (0, 29999));
    CHECK (reversed.getSample (0, 29999) == b.getSample (0, 0));

    const auto gained = readAll (dsp::applyChannelGains (input, { 2.0f, 0.5f }, store));
    CHECK (gained.getSample (0, 15000) == b.getSample (0, 15000) * 2.0f);
    CHECK (gained.getSample (1, 15000) == -0.25f);

    const auto faster = dsp::changeSpeed (input, 200.0, store);
    CHECK (faster.getSampleRate() == rate);
    CHECK (std::abs (faster.getNumFrames() - 15000) <= 1);
}

//==============================================================================
TEST_CASE ("Loudness of a stereo 1 kHz sine at -23 dBFS is -23 LUFS (EBU Tech 3341)", "[loudness]")
{
    const auto amplitude = dsp::dbToGain (-23.0f);
    const auto b = sine (2, (int) rate * 20, 1000.0, amplitude);

    dsp::LoudnessMeter m;
    m.prepare (rate, 2);
    m.process (b.getArrayOfReadPointers(), 2, b.getNumSamples());

    CHECK_THAT (m.getIntegrated(), WithinAbs (-23.0, 0.1));
    CHECK_THAT (m.getMomentary(), WithinAbs (-23.0, 0.1));
    CHECK_THAT (m.getShortTerm(), WithinAbs (-23.0, 0.1));
    CHECK_THAT (m.getLoudnessRange(), WithinAbs (0.0, 0.2));

    // Mono: one channel, 3 dB less (BS.1770 channel sum).
    const auto mono = sine (1, (int) rate * 5, 1000.0, amplitude);
    CHECK_THAT (measureIntegrated (mono), WithinAbs (-26.0, 0.1));

    // Other sample rates measure the same.
    dsp::LoudnessMeter m44;
    m44.prepare (44100.0, 2);
    juce::AudioBuffer<float> b44 (2, 44100 * 5);
    for (int i = 0; i < b44.getNumSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b44.setSample (c, i, amplitude * (float) std::sin (twoPi * 1000.0 * i / 44100.0));
    m44.process (b44.getArrayOfReadPointers(), 2, b44.getNumSamples());
    CHECK_THAT (m44.getIntegrated(), WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Integrated loudness gates quiet parts (EBU Tech 3341 cases 3 and 5)", "[loudness]")
{
    const auto quiet = sine (2, (int) rate * 10, 1000.0, dsp::dbToGain (-36.0f));
    const auto loud = sine (2, (int) rate * 60, 1000.0, dsp::dbToGain (-23.0f));
    CHECK_THAT (measureIntegrated (concat ({ quiet, loud, quiet })), WithinAbs (-23.0, 0.1));

    const auto a = sine (2, (int) rate * 20, 1000.0, dsp::dbToGain (-26.0f));
    const auto b = sine (2, (int) (rate * 20.1), 1000.0, dsp::dbToGain (-20.0f));
    CHECK_THAT (measureIntegrated (concat ({ a, b, a })), WithinAbs (-23.0, 0.1));

    // Digital silence does not count at all (absolute gate).
    juce::AudioBuffer<float> silence (2, (int) rate * 30);
    silence.clear();
    CHECK_THAT (measureIntegrated (concat ({ loud, silence })), WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Loudness range of two levels 10 dB apart is 10 LU (EBU Tech 3342 case 1)", "[loudness]")
{
    const auto a = sine (2, (int) rate * 20, 1000.0, dsp::dbToGain (-20.0f));
    const auto b = sine (2, (int) rate * 20, 1000.0, dsp::dbToGain (-30.0f));
    const auto both = concat ({ a, b });

    dsp::LoudnessMeter m;
    m.prepare (rate, 2);
    m.process (both.getArrayOfReadPointers(), 2, both.getNumSamples());
    CHECK_THAT (m.getLoudnessRange(), WithinAbs (10.0, 0.5));
}

TEST_CASE ("True peak finds the peaks between samples", "[loudness]")
{
    // A full scale sine at a quarter of the sample rate, sampled 45 degrees off
    // its peaks: every sample is 0.707 (-3 dB) but the wave reaches 0 dB.
    const auto b = sine (1, 4800, rate / 4.0, 1.0f, twoPi / 8.0);
    CHECK_THAT (peakDb (b), WithinAbs (-3.01, 0.05));

    dsp::TruePeakDetector tp;
    tp.prepare (1);
    tp.process (b.getArrayOfReadPointers(), 1, b.getNumSamples());
    CHECK_THAT (dsp::gainToDb (tp.getPeak (0)), WithinAbs (0.0, 0.5));

    // A low frequency reads the same as its samples.
    const auto low = sine (1, 48000, 100.0, 0.5f);
    dsp::TruePeakDetector tp2;
    tp2.prepare (1);
    tp2.process (low.getArrayOfReadPointers(), 1, low.getNumSamples());
    CHECK_THAT (dsp::gainToDb (tp2.getPeak (0)), WithinAbs (-6.02, 0.05));
}

TEST_CASE ("Analysis reports peak, RMS, DC offset and clipping", "[loudness][render]")
{
    juce::AudioBuffer<float> b (2, 48000);
    for (int i = 0; i < 48000; ++i)
    {
        b.setSample (0, i, 0.5f * (float) std::sin (twoPi * 1000.0 * i / rate));
        b.setSample (1, i, 0.1f);
    }
    b.setSample (1, 100, 1.0f);

    const auto stats = dsp::analyse (toSequence (b));
    REQUIRE (stats.valid);
    CHECK_THAT (stats.channelPeakDb[0], WithinAbs (-6.02, 0.01));
    CHECK_THAT (stats.channelRmsDb[0], WithinAbs (-9.03, 0.05));
    CHECK_THAT (stats.channelDcOffset[1], WithinAbs (0.1, 0.001));
    CHECK (stats.channelClipped[0] == 0);
    CHECK (stats.channelClipped[1] == 1);
    CHECK_THAT (stats.peakDb, WithinAbs (0.0, 0.01));
}

TEST_CASE ("Match Loudness reaches the target and respects the true peak limit", "[loudness][render]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    // Speech-like: noise bursts with pauses, quiet overall.
    juce::AudioBuffer<float> b = noise (2, (int) rate * 20, 0.05f);
    for (int i = 0; i < b.getNumSamples(); ++i)
        if ((i / 24000) % 3 == 2)
            for (int c = 0; c < 2; ++c)
                b.setSample (c, i, b.getSample (c, i) * 0.02f);
    b.setSample (0, 1000, 0.6f); // one sharp peak

    const auto input = toSequence (b);
    const auto result = dsp::matchLoudness (input, -16.0f, -1.0f, true, store);

    REQUIRE (result.audio.getNumFrames() == input.getNumFrames());
    CHECK (result.limited);
    CHECK_THAT (result.after.integratedLufs, WithinAbs (-16.0, 0.3));
    CHECK (result.after.truePeakDb <= -1.0f + 0.2f);

    // Without a limiter the gain stops at the peak limit.
    const auto unlimited = dsp::matchLoudness (input, -16.0f, -1.0f, false, store);
    CHECK (unlimited.peakLimitReached);
    CHECK (unlimited.after.truePeakDb <= -1.0f + 0.05f);

    // A quiet sine needs only a gain: no new audio is written.
    const auto quietSine = toSequence (sine (2, (int) rate * 5, 1000.0, dsp::dbToGain (-30.0f)));
    const auto simple = dsp::matchLoudness (quietSine, -20.0f, -1.0f, true, store);
    CHECK (! simple.limited);
    CHECK_THAT (simple.gainDb, WithinAbs (10.0, 0.1));
    CHECK_THAT (simple.after.integratedLufs, WithinAbs (-20.0, 0.1));
}

TEST_CASE ("Normalize gains bring the peak to the target", "[loudness]")
{
    dsp::AudioStats stats;
    stats.valid = true;
    stats.numChannels = 2;
    stats.peakDb = -6.0f;
    stats.channelPeakDb = { -6.0f, -12.0f };

    auto together = dsp::normalizeGains (stats, -1.0f, false);
    REQUIRE (together.size() == 2);
    CHECK_THAT (dsp::gainToDb (together[0]), WithinAbs (5.0, 1.0e-4));
    CHECK_THAT (dsp::gainToDb (together[1]), WithinAbs (5.0, 1.0e-4));

    auto separate = dsp::normalizeGains (stats, -1.0f, true);
    CHECK_THAT (dsp::gainToDb (separate[1]), WithinAbs (11.0, 1.0e-4));
}

TEST_CASE ("The spectrum shows a sine at its frequency and level", "[dsp]")
{
    dsp::Spectrum spectrum (12);
    const auto n = spectrum.getSize();
    const auto hz = rate * 100.0 / n; // exactly on bin 100
    const auto b = sine (1, n, hz, 0.5f);

    std::vector<float> db ((size_t) spectrum.getNumBins());
    spectrum.compute (b.getReadPointer (0), db.data());
    CHECK_THAT (db[100], WithinAbs (-6.02, 0.1));
    CHECK (db[300] < -60.0f);
}

TEST_CASE ("The level meter reads peak, RMS, correlation and loudness", "[loudness][engine]")
{
    dsp::LevelMeter meter;
    meter.prepare (rate, 2);
    auto b = sine (2, 48000, 1000.0, 0.5f);
    for (int i = 0; i < 3; ++i) // RMS and correlation settle within about a second
        meter.process (b.getArrayOfReadPointers(), 2, b.getNumSamples());

    CHECK_THAT (dsp::gainToDb (meter.takePeak (0)), WithinAbs (-6.02, 0.01));
    CHECK (meter.takePeak (0) == 0.0f); // taken
    CHECK_THAT (dsp::gainToDb (meter.getRms (1)), WithinAbs (-9.03, 0.1));
    CHECK_THAT (meter.getCorrelation(), WithinAbs (1.0, 0.01));

    // Opposite channels: correlation -1.
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (1, i, -b.getSample (0, i));
    for (int i = 0; i < 3; ++i)
        meter.process (b.getArrayOfReadPointers(), 2, b.getNumSamples());
    CHECK_THAT (meter.getCorrelation(), WithinAbs (-1.0, 0.01));
}

//==============================================================================
TEST_CASE ("Effect settings, chains and presets survive a round trip", "[effects][presets]")
{
    auto comp = dsp::EffectSettings::defaultsFor ("compressor");
    comp.values[0] = -30.0f;
    comp.enabled = false;

    const auto back = dsp::EffectSettings::fromVar (juce::JSON::parse (juce::JSON::toString (comp.toVar())));
    CHECK (back.typeId == "compressor");
    CHECK (back.values == comp.values);
    CHECK (! back.enabled);

    // Unknown types are dropped, unknown parameters ignored, out-of-range values limited.
    const auto chain = dsp::chainFromVar (juce::JSON::parse (
        R"([{"type":"amplify","values":{"gain":99,"nonsense":3}},{"type":"no-such-effect"}])"));
    REQUIRE (chain.size() == 1);
    CHECK (chain[0].values[0] == 40.0f);

    test::TempFolder temp;
    PresetStore store (temp.folder.getChildFile ("presets"));
    CHECK (store.saveEffectPreset ("My voice", comp));
    CHECK (store.saveEffectPreset ("Another", dsp::EffectSettings::defaultsFor ("compressor")));
    comp.values[0] = -10.0f;
    CHECK (store.saveEffectPreset ("My voice", comp)); // replaces

    auto presets = store.getEffectPresets ("compressor");
    REQUIRE (presets.size() == 2);
    CHECK (presets[0].name == "Another");
    CHECK (presets[1].settings.values[0] == -10.0f);
    CHECK (store.deleteEffectPreset ("compressor", "Another"));
    CHECK (store.getEffectPresets ("compressor").size() == 1);
    CHECK (store.getEffectPresets ("limiter").empty());

    CHECK (store.saveRackPreset ("Podcast chain", { comp, dsp::EffectSettings::defaultsFor ("limiter") }));
    const auto racks = store.getRackPresets();
    REQUIRE (racks.size() == 1);
    CHECK (racks[0].chain.size() == 2);
    CHECK (racks[0].chain[1].typeId == "limiter");
}

TEST_CASE ("Replacing a range by processed audio keeps or moves the markers", "[effects][edit]")
{
    juce::AudioBuffer<float> b (1, 1000);
    b.clear();
    AudioState s { toSequence (b), { { 100, "a" }, { 500, "b" }, { 900, "c" } } };

    juce::AudioBuffer<float> same (1, 400);
    same.clear();
    auto r = edits::replaceRange (s, { 400, 800 }, toSequence (same));
    REQUIRE (r.changed);
    CHECK (r.state.audio.getNumFrames() == 1000);
    CHECK (r.state.markers == s.markers);
    CHECK (r.selection == FrameRange (400, 800));

    juce::AudioBuffer<float> half (1, 200);
    half.clear();
    r = edits::replaceRange (s, { 400, 800 }, toSequence (half));
    CHECK (r.state.audio.getNumFrames() == 800);
    REQUIRE (r.state.markers.size() == 3);
    CHECK (r.state.markers[0].position == 100);
    CHECK (r.state.markers[1].position == 450);
    CHECK (r.state.markers[2].position == 700);
    CHECK (r.selection == FrameRange (400, 600));
}

TEST_CASE ("Playback runs through the effect chain and the meter", "[effects][engine]")
{
    PlaybackEngine engine;
    engine.prepare (rate, 256);

    juce::AudioBuffer<float> b (2, 20000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 20000; ++i)
            b.setSample (c, i, 0.25f);
    engine.setSequence (toSequence (b));

    dsp::ChainSettings chain { dsp::EffectSettings::defaultsFor ("amplify") };
    chain[0].values[0] = 6.0206f; // x2
    engine.setEffectChain (dsp::ProcessorChain::create (chain));
    CHECK (engine.getEffectChainSampleRate() == rate);

    REQUIRE (engine.play (0));
    REQUIRE (engine.waitUntilBuffered (2000));

    juce::AudioBuffer<float> block (2, 256);
    engine.audioDeviceIOCallbackWithContext (nullptr, 0, block.getArrayOfWritePointers(), 2, 256, {});
    CHECK_THAT (block.getSample (0, 100), WithinAbs (0.5, 1.0e-3));
    CHECK_THAT (dsp::gainToDb (engine.getMeter().takePeak (1)), WithinAbs (-6.02, 0.05));

    std::vector<float> recent (1024);
    CHECK (engine.readRecentOutput (recent.data(), 1024) == 256);
    CHECK_THAT (recent[10], WithinAbs (0.5, 1.0e-3));

    // Removing the chain: the old one comes back to this thread and is deleted here.
    engine.setEffectChain (nullptr);
    engine.audioDeviceIOCallbackWithContext (nullptr, 0, block.getArrayOfWritePointers(), 2, 256, {});
    engine.collectRetiredChain();
    CHECK_THAT (block.getSample (0, 100), WithinAbs (0.25, 1.0e-6));
    engine.stop();
}
