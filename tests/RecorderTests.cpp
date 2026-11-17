// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "engine/recording/Recorder.h"

#include <catch2/catch_test_macros.hpp>

using namespace mxj;

namespace
{
    /** Pretends to be the audio device: feeds `input` to the callback in blocks,
        the way a sound card delivers microphone audio. */
    void feed (Recorder& recorder, const juce::AudioBuffer<float>& input, int blockSize)
    {
        juce::AudioBuffer<float> out (2, blockSize);
        for (int pos = 0; pos < input.getNumSamples(); pos += blockSize)
        {
            const auto n = juce::jmin (blockSize, input.getNumSamples() - pos);
            const float* in[2] = { input.getReadPointer (0, pos), input.getNumChannels() > 1 ? input.getReadPointer (1, pos) : nullptr };
            recorder.audioDeviceIOCallbackWithContext (in, input.getNumChannels(), out.getArrayOfWritePointers(), 2, n, {});

            // A real device delivers in real time; give the writer thread a moment now and then.
            if ((pos / blockSize) % 64 == 0)
                juce::Thread::sleep (1);
        }
    }

    juce::AudioBuffer<float> makeInput (int channels, int frames)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, (float) std::sin (i * 0.01 + c) * 0.8f);
        return b;
    }
}

TEST_CASE ("A recording contains exactly what came in", "[recording]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    Recorder recorder;

    REQUIRE (recorder.start (store, 2, 48000.0));
    CHECK_FALSE (recorder.start (store, 2, 48000.0)); // one take at a time

    const auto input = makeInput (2, 100000);
    feed (recorder, input, 480);

    CHECK (recorder.takePeakLevel (0) > 0.7f);
    CHECK (recorder.takePeakLevel (0) == 0.0f); // reset after reading

    const auto take = recorder.stop();
    CHECK_FALSE (recorder.isRecording());
    CHECK (recorder.getDroppedFrames() == 0);
    REQUIRE (take.getNumFrames() == 100000);
    REQUIRE (take.getNumChannels() == 2);

    juce::AudioBuffer<float> got (2, 100000);
    take.read (got.getArrayOfWritePointers(), 2, 0, 100000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 100000; ++i)
            REQUIRE (got.getSample (c, i) == input.getSample (c, i));

    float lo, hi;
    REQUIRE (take.getMinMax (0, 0, 100000, lo, hi));
    CHECK (hi > 0.79f);
}

TEST_CASE ("A mono microphone fills both channels of a stereo take", "[recording]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    Recorder recorder;

    REQUIRE (recorder.start (store, 2, 44100.0));
    const auto input = makeInput (1, 5000);
    feed (recorder, input, 256);
    const auto take = recorder.stop();

    juce::AudioBuffer<float> got (2, 5000);
    REQUIRE (take.getNumFrames() == 5000);
    take.read (got.getArrayOfWritePointers(), 2, 0, 5000);
    CHECK (got.getSample (1, 1234) == input.getSample (0, 1234));
}

TEST_CASE ("A long take is split into segments with peaks, readable while recording", "[recording]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    Recorder recorder;

    // At 100 Hz a ten minute segment is 60000 frames.
    REQUIRE (recorder.start (store, 1, 100.0));
    const auto input = makeInput (1, 150000);
    feed (recorder, input, 1000);

    // Wait for the writer thread, then draw what is there.
    for (int i = 0; i < 200 && recorder.getFramesRecorded() < 150000; ++i)
        juce::Thread::sleep (5);

    float lo, hi;
    CHECK (recorder.getMinMax (0, 0, 150000, lo, hi));
    CHECK (recorder.getSegmentFiles().size() == 3);

    const auto take = recorder.stop();
    CHECK (take.getNumFrames() == 150000);
    CHECK (take.getBlocks().size() == 3);
}

TEST_CASE ("Stopping without input gives an empty take", "[recording]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    Recorder recorder;

    REQUIRE (recorder.start (store, 1, 48000.0));
    CHECK (recorder.stop().isEmpty());
    CHECK (recorder.stop().isEmpty()); // stopping twice is harmless
}
