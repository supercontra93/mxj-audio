// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "core/audio/AudioSequence.h"
#include "core/audio/BlockStore.h"
#include "core/audio/PeakCache.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace mxj;

namespace
{
    /** Channel c, frame i holds (c + 1) * 1000 + i, scaled down, so every value is unique. */
    juce::AudioBuffer<float> makeRamp (int channels, int frames)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, (float) ((c + 1) * 100000 + i) / 1.0e6f);
        return b;
    }

    float rampValue (int channel, juce::int64 frame) { return (float) ((channel + 1) * 100000 + frame) / 1.0e6f; }
}

TEST_CASE ("A sequence reads across block boundaries", "[audio]")
{
    auto source = std::make_shared<MemorySampleSource> (makeRamp (2, 10000), 48000.0);
    const auto seq = AudioSequence::fromSource (source, 1000);

    CHECK (seq.getBlocks().size() == 10);
    CHECK (seq.getNumFrames() == 10000);
    CHECK (seq.getNumChannels() == 2);

    juce::AudioBuffer<float> out (2, 2500);
    REQUIRE (seq.read (out.getArrayOfWritePointers(), 2, 900, 2500));

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 2500; ++i)
            REQUIRE (out.getSample (c, i) == rampValue (c, 900 + i));
}

TEST_CASE ("Reading outside a sequence gives silence", "[audio]")
{
    auto source = std::make_shared<MemorySampleSource> (makeRamp (1, 100), 44100.0);
    const auto seq = AudioSequence::fromSource (source, 30);

    juce::AudioBuffer<float> out (2, 50);
    REQUIRE (seq.read (out.getArrayOfWritePointers(), 2, 80, 50));

    for (int i = 0; i < 50; ++i)
    {
        CHECK (out.getSample (0, i) == (i < 20 ? rampValue (0, 80 + i) : 0.0f));
        CHECK (out.getSample (1, i) == 0.0f); // the source is mono
    }

    REQUIRE (seq.read (out.getArrayOfWritePointers(), 1, -10, 20));
    CHECK (out.getSample (0, 0) == 0.0f);
    CHECK (out.getSample (0, 10) == rampValue (0, 0));
}

TEST_CASE ("A sequence refuses blocks that do not fit", "[audio]")
{
    auto stereo = std::make_shared<MemorySampleSource> (makeRamp (2, 100), 48000.0);
    auto mono = std::make_shared<MemorySampleSource> (makeRamp (1, 100), 48000.0);
    auto otherRate = std::make_shared<MemorySampleSource> (makeRamp (2, 100), 44100.0);

    AudioSequence seq (2, 48000.0);
    CHECK (seq.append ({ stereo, 0, 100 }));
    CHECK_FALSE (seq.append ({ mono, 0, 100 }));
    CHECK_FALSE (seq.append ({ otherRate, 0, 100 }));
    CHECK_FALSE (seq.append ({ stereo, 50, 51 }));
    CHECK (seq.getNumFrames() == 100);
}

TEST_CASE ("The block store writes float audio and reads it back", "[audio]")
{
    test::TempFolder temp;
    juce::File written;

    {
        BlockStore store (temp.folder);
        auto writer = store.createWriter (3, 96000.0);
        const auto data = makeRamp (3, 5000);

        // Write in uneven pieces.
        for (int pos = 0; pos < 5000; pos += 1234)
        {
            const auto n = juce::jmin (1234, 5000 - pos);
            const float* pointers[] { data.getReadPointer (0, pos), data.getReadPointer (1, pos), data.getReadPointer (2, pos) };
            REQUIRE (writer->append (pointers, n));
        }

        auto source = writer->finish();
        REQUIRE (source != nullptr);
        written = source->getFile();
        CHECK (written.existsAsFile());
        CHECK (source->getNumFrames() == 5000);
        CHECK (source->getNumChannels() == 3);

        juce::AudioBuffer<float> out (3, 300);
        REQUIRE (source->read (out.getArrayOfWritePointers(), 3, 4800, 300));
        for (int c = 0; c < 3; ++c)
        {
            CHECK (out.getSample (c, 0) == rampValue (c, 4800));
            CHECK (out.getSample (c, 199) == rampValue (c, 4999));
            CHECK (out.getSample (c, 200) == 0.0f);
        }

        source = nullptr;
        CHECK_FALSE (written.existsAsFile()); // the file goes with its last user
    }

    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0);
}

TEST_CASE ("Abandoned block folders are cleaned up", "[audio]")
{
    test::TempFolder temp;
    temp.folder.getChildFile ("run-20260101-000000").getChildFile ("000001.f32").create();

    CHECK (BlockStore::cleanUpAbandonedFolders (temp.folder) == 1);
    CHECK_FALSE (temp.folder.getChildFile ("run-20260101-000000").exists());
}

TEST_CASE ("Peaks match the audio at every zoom level", "[audio][peaks]")
{
    constexpr int frames = 300000;
    juce::AudioBuffer<float> audio (2, frames);
    juce::Random random (42);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            audio.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);

    PeakCache peaks (2, frames);

    // Feed it in odd sized chunks like a decoder would.
    for (int pos = 0; pos < frames; pos += 7777)
    {
        const auto n = juce::jmin (7777, frames - pos);
        const float* p[] { audio.getReadPointer (0, pos), audio.getReadPointer (1, pos) };
        peaks.append (p, n);
    }

    REQUIRE (peaks.isComplete());

    // A range aligned to level 0 entries must be exact at any span.
    for (const juce::int64 span : { 128LL, 1024LL, 5000LL * 128, 100000LL, 299904LL })
    {
        const juce::int64 start = 128 * 3;
        const auto end = juce::jmin (start + span, (juce::int64) frames);
        float lo = 0, hi = 0;
        REQUIRE (peaks.getMinMax (1, start, end, lo, hi));

        const auto exact = juce::FloatVectorOperations::findMinAndMax (audio.getReadPointer (1, (int) start), (int) (end - start));
        // Coarse levels may include a few neighbouring frames, never miss any.
        CHECK (lo <= exact.getStart());
        CHECK (hi >= exact.getEnd());

        if (span <= 1024)
        {
            CHECK (lo == exact.getStart());
            CHECK (hi == exact.getEnd());
        }
    }

    float lo, hi;
    REQUIRE (peaks.getMinMax (0, 0, frames, lo, hi));
    const auto whole = audio.findMinMax (0, 0, frames);
    CHECK (lo == whole.getStart());
    CHECK (hi == whole.getEnd());
}

TEST_CASE ("Peaks can be drawn while they are still being built", "[audio][peaks]")
{
    juce::AudioBuffer<float> audio (1, 10000);
    audio.clear();
    audio.setSample (0, 100, 0.5f);
    audio.setSample (0, 9000, -0.75f);

    PeakCache peaks (1, 10000);
    const float* p[] { audio.getReadPointer (0) };
    peaks.append (p, 5000);

    CHECK_FALSE (peaks.isComplete());
    CHECK (peaks.getFramesReady() == 5000);

    float lo, hi;
    REQUIRE (peaks.getMinMax (0, 0, 10000, lo, hi));
    CHECK (hi == 0.5f);
    CHECK (lo == 0.0f);
    CHECK_FALSE (peaks.getMinMax (0, 6000, 10000, lo, hi));

    const float* rest[] { audio.getReadPointer (0, 5000) };
    peaks.append (rest, 5000);
    REQUIRE (peaks.getMinMax (0, 0, 10000, lo, hi));
    CHECK (lo == -0.75f);
}

TEST_CASE ("Peaks survive a save and load, and stale files are refused", "[audio][peaks]")
{
    test::TempFolder temp;
    const auto file = temp.folder.getChildFile ("test.mxjpeaks");

    juce::AudioBuffer<float> audio (2, 3000);
    for (int i = 0; i < 3000; ++i)
    {
        audio.setSample (0, i, std::sin ((float) i * 0.01f));
        audio.setSample (1, i, 0.25f * std::cos ((float) i * 0.02f));
    }

    PeakCache peaks (2, 3000);
    const float* p[] { audio.getReadPointer (0), audio.getReadPointer (1) };
    peaks.append (p, 3000);
    REQUIRE (peaks.save (file));

    const auto loaded = PeakCache::load (file, 2, 3000);
    REQUIRE (loaded != nullptr);
    CHECK (loaded->isComplete());

    for (juce::int64 start = 0; start < 3000; start += 250)
    {
        float a0, a1, b0, b1;
        REQUIRE (peaks.getMinMax (1, start, start + 700, a0, a1));
        REQUIRE (loaded->getMinMax (1, start, start + 700, b0, b1));
        CHECK (a0 == b0);
        CHECK (a1 == b1);
    }

    CHECK (PeakCache::load (file, 1, 3000) == nullptr);
    CHECK (PeakCache::load (file, 2, 3001) == nullptr);
    CHECK (PeakCache::load (temp.folder.getChildFile ("missing"), 2, 3000) == nullptr);
}
