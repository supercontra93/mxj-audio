// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/playback/PlaybackEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace mxj;

namespace
{
    AudioSequence makeSequence (int channels, int frames, double rate, std::function<float (int, int)> value)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, value (c, i));

        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate), 10000);
    }

    /** Pretends to be the audio device: calls the callback block by block, waiting
        for the disk thread whenever the FIFO runs dry, like a real device would
        simply play silence. Returns everything that came out. */
    juce::AudioBuffer<float> pump (PlaybackEngine& engine, int outputChannels, int totalFrames, int blockSize)
    {
        juce::AudioBuffer<float> all (outputChannels, totalFrames);
        all.clear();
        juce::AudioBuffer<float> block (outputChannels, blockSize);
        int written = 0;
        int idleRounds = 0;

        while (written < totalFrames && engine.isPlaying() && idleRounds < 2000)
        {
            const auto before = engine.getDeviceFramesPlayed();
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, block.getArrayOfWritePointers(), outputChannels,
                                                     blockSize, {});

            // Keep only the frames that really came from the FIFO.
            const auto played = (int) (engine.getDeviceFramesPlayed() - before);
            if (played == 0)
            {
                ++idleRounds;
                juce::Thread::sleep (1);
                continue;
            }

            const auto n = juce::jmin (played, totalFrames - written);
            for (int c = 0; c < outputChannels; ++c)
                all.copyFrom (c, written, block, c, 0, n);
            written += n;
        }

        all.setSize (outputChannels, written, true);
        return all;
    }
}

TEST_CASE ("Playback reproduces the audio exactly when the rates match", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare (48000.0, 256);
    engine.setSequence (makeSequence (2, 100000, 48000.0, [] (int c, int i) { return (float) ((c + 1) * 100000 + i) / 1.0e6f; }));

    REQUIRE (engine.play (1000));
    REQUIRE (engine.waitUntilBuffered (2000));
    CHECK (engine.isPlaying());

    const auto out = pump (engine, 2, 99000, 256);
    REQUIRE (out.getNumSamples() == 99000);

    for (int i = 0; i < out.getNumSamples(); i += 97)
    {
        REQUIRE (out.getSample (0, i) == (float) (100000 + 1000 + i) / 1.0e6f);
        REQUIRE (out.getSample (1, i) == (float) (200000 + 1000 + i) / 1.0e6f);
    }

    // The end of the audio stops playback by itself, once.
    float silence[2][256];
    float* outs[] { silence[0], silence[1] };
    for (int i = 0; i < 10 && engine.isPlaying(); ++i)
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 256, {});

    CHECK_FALSE (engine.isPlaying());
    CHECK (engine.takeFinishedFlag());
    CHECK_FALSE (engine.takeFinishedFlag());
    CHECK (engine.getPosition() == 100000);
}

TEST_CASE ("A mono file plays on both speakers and on a mono device", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare (44100.0, 128);
    engine.setSequence (makeSequence (1, 20000, 44100.0, [] (int, int i) { return (float) i / 20000.0f; }));

    REQUIRE (engine.play (0));
    auto out = pump (engine, 2, 10000, 128);
    REQUIRE (out.getNumSamples() == 10000);
    CHECK (out.getSample (0, 5000) == 0.25f);
    CHECK (out.getSample (1, 5000) == 0.25f);

    REQUIRE (engine.play (0));
    out = pump (engine, 1, 10000, 128);
    REQUIRE (out.getNumSamples() == 10000);
    CHECK_THAT (out.getSample (0, 5000), Catch::Matchers::WithinAbs (0.25, 1.0e-6));
}

TEST_CASE ("Playback converts the sample rate when the device differs", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare (48000.0, 512);

    // One second of 1 kHz at 44.1 kHz should become about one second at 48 kHz.
    engine.setSequence (makeSequence (1, 44100, 44100.0, [] (int, int i)
    {
        return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 44100.0);
    }));

    REQUIRE (engine.play (0));
    const auto out = pump (engine, 2, 60000, 512);

    CHECK (out.getNumSamples() > 47900);
    CHECK (out.getNumSamples() < 48100);

    // 1 kHz at 48 kHz is one rising zero crossing every 48 frames. Skip the
    // first and last frames, where the resampling filter starts and ends.
    int crossings = 0;
    for (int i = 4801; i < 4800 + 38400; ++i)
        if (out.getSample (0, i - 1) < 0.0f && out.getSample (0, i) >= 0.0f)
            ++crossings;

    CHECK (crossings == 800);
    CHECK (out.getMagnitude (0, 1000, 40000) < 0.52f);
    CHECK (out.getMagnitude (0, 1000, 40000) > 0.48f);
}

TEST_CASE ("Play needs audio and a device, and stop is immediate", "[playback]")
{
    PlaybackEngine engine;
    engine.setSequence (makeSequence (2, 1000, 48000.0, [] (int, int) { return 0.1f; }));
    CHECK_FALSE (engine.play (0)); // no device yet

    engine.prepare (48000.0, 256);
    engine.setSequence ({});
    CHECK_FALSE (engine.play (0)); // nothing to play

    engine.setSequence (makeSequence (2, 48000, 48000.0, [] (int, int) { return 0.1f; }));
    REQUIRE (engine.play (24000));
    CHECK (engine.getStartPosition() == 24000);
    engine.stop();
    CHECK_FALSE (engine.isPlaying());

    float buffer[2][64] {};
    buffer[0][0] = 1.0f;
    float* outs[] { buffer[0], buffer[1] };
    engine.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 64, {});
    CHECK (buffer[0][0] == 0.0f); // stopped engine writes silence
}

TEST_CASE ("The test tone plays without a file", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare (48000.0, 480);
    engine.setTestToneEnabled (true);

    juce::AudioBuffer<float> out (2, 480);
    engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 480, {});
    CHECK_THAT (out.getMagnitude (0, 0, 480), Catch::Matchers::WithinAbs (0.1, 0.001));
    CHECK (out.getMagnitude (1, 0, 480) > 0.09f);
}
