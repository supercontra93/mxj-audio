// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "dsp/effects/EffectCatalogue.h"
#include "dsp/repair/NoiseReduction.h"
#include "dsp/repair/Restoration.h"
#include "dsp/spectral/SpectralEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace mxj;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    juce::AudioBuffer<float> sines (int channels, int frames, std::initializer_list<std::pair<double, float>> parts)
    {
        juce::AudioBuffer<float> b (channels, frames);
        b.clear();
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                for (auto [hz, amp] : parts)
                    b.addSample (c, i, amp * (float) std::sin (twoPi * hz * i / rate + c * 0.3));
        return b;
    }

    void addNoise (juce::AudioBuffer<float>& b, float amplitude, int seed = 7)
    {
        juce::Random r (seed);
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.addSample (c, i, amplitude * (r.nextFloat() * 2.0f - 1.0f));
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

    float rmsDb (const juce::AudioBuffer<float>& b, int start, int length, int channel = 0)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (channel, start, length), -200.0f);
    }

    /** Level of one frequency in a buffer (correlation with a sine and cosine). */
    float toneDb (const juce::AudioBuffer<float>& b, double hz, int start, int length, int channel = 0)
    {
        double re = 0, im = 0;
        for (int i = start; i < start + length; ++i)
        {
            re += b.getSample (channel, i) * std::cos (twoPi * hz * i / rate);
            im += b.getSample (channel, i) * std::sin (twoPi * hz * i / rate);
        }
        const auto amplitude = 2.0 * std::sqrt (re * re + im * im) / length;
        return (float) (20.0 * std::log10 (amplitude + 1.0e-12));
    }

    AudioSequence applyEffect (const AudioSequence& input, const dsp::EffectSettings& settings, BlockStore& store)
    {
        auto chain = dsp::ProcessorChain::create ({ settings });
        return dsp::render (input, *chain, store);
    }
}

TEST_CASE ("Real FFT matches a plain DFT and inverts exactly", "[spectral]")
{
    dsp::RealFft fft (8);
    const int n = fft.getSize();
    std::vector<float> x ((size_t) n), back ((size_t) n);
    juce::Random r (3);
    for (auto& v : x)
        v = r.nextFloat() * 2.0f - 1.0f;

    std::vector<std::complex<float>> bins ((size_t) fft.getNumBins());
    fft.forward (x.data(), bins.data());

    for (int k = 0; k < fft.getNumBins(); k += 7)
    {
        std::complex<double> sum;
        for (int i = 0; i < n; ++i)
            sum += (double) x[(size_t) i] * std::polar (1.0, -twoPi * k * i / n);
        CHECK_THAT (bins[(size_t) k].real(), WithinAbs (sum.real(), 1.0e-3));
        CHECK_THAT (bins[(size_t) k].imag(), WithinAbs (sum.imag(), 1.0e-3));
    }

    fft.inverse (bins.data(), back.data());
    for (int i = 0; i < n; ++i)
        CHECK_THAT (back[(size_t) i], WithinAbs (x[(size_t) i], 1.0e-5));
}

TEST_CASE ("STFT without changes gives the audio back, also across chunks", "[spectral]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    auto buffer = sines (2, 100000, { { 440.0, 0.4f }, { 3000.0, 0.2f } });
    addNoise (buffer, 0.05f);
    const auto input = toSequence (buffer);

    for (auto range : { FrameRange (0, 100000), FrameRange (12345, 80000), FrameRange (99000, 100000) })
    {
        const auto out = dsp::stft::process (input, range, 11, [] (const dsp::stft::Frame&) {}, store);
        REQUIRE (out.getNumFrames() == range.getLength());
        const auto result = readAll (out);

        float maxError = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < result.getNumSamples(); ++i)
                maxError = juce::jmax (maxError, std::abs (result.getSample (c, i) - buffer.getSample (c, (int) range.getStart() + i)));

        CHECK (maxError < 1.0e-4f);
    }
}

TEST_CASE ("Frequency scales map both ways", "[spectral]")
{
    for (auto kind : { dsp::FrequencyScale::Kind::linear, dsp::FrequencyScale::Kind::logarithmic, dsp::FrequencyScale::Kind::mel })
    {
        dsp::FrequencyScale scale { kind, 24000.0 };
        for (double hz : { 50.0, 440.0, 1000.0, 8000.0, 20000.0 })
            CHECK_THAT (scale.fromProportion (scale.toProportion (hz)), WithinAbs (hz, hz * 1.0e-6));

        CHECK_THAT (scale.toProportion (24000.0), WithinAbs (1.0, 1.0e-9));
        CHECK (scale.toProportion (1000.0) < scale.toProportion (2000.0));
    }
}

TEST_CASE ("Deleting a frequency band removes only that band and only in the selection", "[spectral]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    const auto buffer = sines (1, 96000, { { 1000.0, 0.3f }, { 5000.0, 0.3f } });
    const auto input = toSequence (buffer);

    dsp::SpectralRegion region { { 30000, 60000 }, 800.0, 1200.0 };
    const auto range = dsp::spectral::editRange (input, region.time);
    const auto out = dsp::spectral::changeGain (input, region, -200.0f, store);
    REQUIRE (out.getNumFrames() == range.getLength());

    auto whole = input;
    whole.replace (range.getStart(), range.getLength(), out);
    const auto result = readAll (whole);

    // Inside: 1 kHz gone, 5 kHz kept.
    CHECK (toneDb (result, 1000.0, 35000, 20000) < toneDb (buffer, 1000.0, 35000, 20000) - 40.0f);
    CHECK_THAT (toneDb (result, 5000.0, 35000, 20000), WithinAbs (toneDb (buffer, 5000.0, 35000, 20000), 0.2));

    // Outside the edit range: untouched.
    CHECK_THAT (toneDb (result, 1000.0, 0, 20000), WithinAbs (toneDb (buffer, 1000.0, 0, 20000), 0.01));
}

TEST_CASE ("Healing lowers a loud burst to the level of its surroundings", "[spectral]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    // Steady background with a loud noise burst (a "cough") in the middle.
    auto buffer = sines (1, 96000, { { 300.0, 0.05f } });
    addNoise (buffer, 0.02f);
    juce::Random r (11);
    for (int i = 40000; i < 46000; ++i)
        buffer.addSample (0, i, 0.6f * (r.nextFloat() * 2.0f - 1.0f));

    const auto input = toSequence (buffer);
    const dsp::SpectralRegion region { { 39500, 46500 } };
    const auto out = dsp::spectral::heal (input, region, store);
    const auto range = dsp::spectral::editRange (input, region.time);
    REQUIRE (out.getNumFrames() == range.getLength());

    auto whole = input;
    whole.replace (range.getStart(), range.getLength(), out);
    const auto result = readAll (whole);

    const auto around = rmsDb (buffer, 20000, 15000);
    CHECK (rmsDb (buffer, 41000, 4000) > around + 15.0f);
    CHECK (rmsDb (result, 41000, 4000) < around + 3.0f);
    CHECK (toneDb (result, 300.0, 41000, 4000) > toneDb (buffer, 300.0, 20000, 4000) - 6.0f); // the background tone survives
}

TEST_CASE ("An effect on a frequency band changes only that band", "[spectral]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    const auto buffer = sines (2, 96000, { { 500.0, 0.3f }, { 4000.0, 0.3f } });
    const auto input = toSequence (buffer);

    // "Processed": the whole edit range silenced.
    const dsp::SpectralRegion region { { 20000, 70000 }, 3000.0, 6000.0 };
    const auto range = dsp::spectral::editRange (input, region.time);
    auto processed = input;
    juce::AudioBuffer<float> silence (2, (int) range.getLength());
    silence.clear();
    processed.replace (range.getStart(), range.getLength(), toSequence (silence));

    const auto out = dsp::spectral::blend (input, processed, region, store);
    auto whole = input;
    whole.replace (range.getStart(), range.getLength(), out);
    const auto result = readAll (whole);

    for (int c = 0; c < 2; ++c)
    {
        CHECK (toneDb (result, 4000.0, 30000, 30000, c) < -40.0f);
        CHECK_THAT (toneDb (result, 500.0, 30000, 30000, c), WithinAbs (toneDb (buffer, 500.0, 30000, 30000, c), 0.2));
    }
}

TEST_CASE ("Noise reduction with a noise print removes steady noise and keeps the tone", "[repair]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    // 1 s of noise alone, then 2 s of tone in the same noise.
    juce::AudioBuffer<float> buffer (1, 144000);
    buffer.clear();
    addNoise (buffer, 0.02f, 5);
    for (int i = 48000; i < 144000; ++i)
        buffer.addSample (0, i, 0.3f * (float) std::sin (twoPi * 1000.0 * i / rate));
    const auto input = toSequence (buffer);

    auto profile = std::make_shared<dsp::NoiseProfile> (dsp::NoiseProfile::capture (input.getRange (0, 48000)));
    REQUIRE (profile->isValid());
    dsp::noisePrint::set (profile);

    auto settings = dsp::EffectSettings::defaultsFor ("noiseReduction");
    settings.values[0] = 24.0f; // reduction
    const auto result = readAll (applyEffect (input, settings, store));
    dsp::noisePrint::set (nullptr);

    REQUIRE (result.getNumSamples() == buffer.getNumSamples());
    CHECK (rmsDb (result, 10000, 30000) < rmsDb (buffer, 10000, 30000) - 15.0f);
    CHECK_THAT (toneDb (result, 1000.0, 80000, 40000), WithinAbs (toneDb (buffer, 1000.0, 80000, 40000), 1.0));
}

TEST_CASE ("Adaptive noise reduction learns the noise by itself", "[repair]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);

    // Speech-like: a tone that comes and goes, in steady noise.
    juce::AudioBuffer<float> buffer (1, 240000);
    buffer.clear();
    addNoise (buffer, 0.02f, 9);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if ((i / 24000) % 2 == 1)
            buffer.addSample (0, i, 0.3f * (float) std::sin (twoPi * 700.0 * i / rate));
    const auto input = toSequence (buffer);

    auto settings = dsp::EffectSettings::defaultsFor ("adaptiveNoiseReduction");
    settings.values[0] = 20.0f;
    const auto result = readAll (applyEffect (input, settings, store));

    // A pause late in the file (noise only): clearly quieter. A tone part: kept.
    CHECK (rmsDb (result, 196000, 16000) < rmsDb (buffer, 196000, 16000) - 10.0f);
    CHECK_THAT (toneDb (result, 700.0, 172000, 16000), WithinAbs (toneDb (buffer, 700.0, 172000, 16000), 1.5));
}

TEST_CASE ("Hum removal takes out 50 Hz and its harmonics and keeps the rest", "[repair]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    const auto buffer = sines (1, 96000, { { 50.0, 0.2f }, { 100.0, 0.1f }, { 150.0, 0.1f }, { 1000.0, 0.3f } });
    const auto result = readAll (applyEffect (toSequence (buffer), dsp::EffectSettings::defaultsFor ("humRemoval"), store));

    for (double hz : { 50.0, 100.0, 150.0 })
        CHECK (toneDb (result, hz, 48000, 48000) < toneDb (buffer, hz, 48000, 48000) - 25.0f);
    CHECK_THAT (toneDb (result, 1000.0, 48000, 48000), WithinAbs (toneDb (buffer, 1000.0, 48000, 48000), 0.5));
}

TEST_CASE ("Interpolation fills a gap in a smooth signal", "[repair]")
{
    const auto clean = sines (1, 2048, { { 440.0, 0.5f }, { 1320.0, 0.2f } });
    auto damaged = clean;
    for (int i = 1000; i < 1040; ++i)
        damaged.setSample (0, i, 0.0f);

    REQUIRE (dsp::restore::interpolate (damaged.getWritePointer (0), 2048, 1000, 40, 32));

    float maxError = 0.0f;
    for (int i = 1000; i < 1040; ++i)
        maxError = juce::jmax (maxError, std::abs (damaged.getSample (0, i) - clean.getSample (0, i)));
    CHECK (maxError < 0.01f);
}

TEST_CASE ("Click removal finds clicks and restores the waveform", "[repair]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    auto clean = sines (1, 48000, { { 300.0, 0.3f }, { 2200.0, 0.1f } });
    addNoise (clean, 0.002f);
    auto damaged = clean;
    const int positions[] { 5000, 12345, 20000, 33333, 41000 };
    for (auto p : positions)
        for (int i = 0; i < 4; ++i)
            damaged.addSample (0, p + i, (i % 2 == 0 ? 0.6f : -0.5f));

    dsp::restore::Report report;
    const auto out = dsp::restore::removeClicks (toSequence (damaged), 50.0f, 2.0f, store, {}, &report);
    const auto result = readAll (out);

    CHECK (report.repaired >= 5);
    for (auto p : positions)
    {
        float error = 0.0f;
        for (int i = p - 4; i < p + 8; ++i)
            error = juce::jmax (error, std::abs (result.getSample (0, i) - clean.getSample (0, i)));
        CHECK (error < 0.05f);
    }

    // The rest is untouched.
    CHECK_THAT (result.getSample (0, 30000), WithinAbs (clean.getSample (0, 30000), 1.0e-6));
}

TEST_CASE ("Clip repair rebuilds cut-off peaks", "[repair]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    const auto clean = sines (1, 48000, { { 200.0, 1.0f } });
    auto clipped = clean;
    for (int i = 0; i < clipped.getNumSamples(); ++i)
        clipped.setSample (0, i, juce::jlimit (-0.7f, 0.7f, clipped.getSample (0, i)));

    dsp::restore::Report report;
    const auto result = readAll (dsp::restore::repairClipping (toSequence (clipped), 1.0f, false, store, {}, &report));

    CHECK (report.repaired > 100);
    CHECK (result.getMagnitude (0, 0, result.getNumSamples()) > 0.85f);

    // Much closer to the original wave than the clipped one.
    double errorBefore = 0, errorAfter = 0;
    for (int i = 0; i < clean.getNumSamples(); ++i)
    {
        errorBefore += std::abs (clipped.getSample (0, i) - clean.getSample (0, i));
        errorAfter += std::abs (result.getSample (0, i) - clean.getSample (0, i));
    }
    CHECK (errorAfter < errorBefore * 0.5);

    // keepLevel: the peaks stay where they were.
    const auto kept = readAll (dsp::restore::repairClipping (toSequence (clipped), 1.0f, true, store));
    CHECK (kept.getMagnitude (0, 0, kept.getNumSamples()) <= 0.7001f);
}
