// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "formats/import/AudioImporter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace mxj;

namespace
{
    juce::AudioBuffer<float> makeSine (int channels, int frames, double rate, double frequency, float gain)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, gain * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate + c));
        return b;
    }

    template <typename Format>
    juce::File writeFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, int bits)
    {
        Format format;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (audio.getNumChannels())
                                                          .withBitsPerSample (bits));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
        return file;
    }

    struct Imported
    {
        std::unique_ptr<AudioImporter> importer;
        AudioImporter::Result result = AudioImporter::Result::failed;
    };

    Imported importFile (const juce::File& file, BlockStore& store, const juce::File& peakFolder = {})
    {
        Imported out;
        juce::String error;
        out.importer = AudioImporter::create (file, store, peakFolder, error);
        INFO (error);
        REQUIRE (out.importer != nullptr);
        out.result = out.importer->run ([] { return true; });
        return out;
    }

    float maxDifference (const AudioSequence& seq, const juce::AudioBuffer<float>& expected)
    {
        juce::AudioBuffer<float> got (expected.getNumChannels(), expected.getNumSamples());
        REQUIRE (seq.read (got.getArrayOfWritePointers(), got.getNumChannels(), 0, got.getNumSamples()));

        float worst = 0;
        for (int c = 0; c < got.getNumChannels(); ++c)
            for (int i = 0; i < got.getNumSamples(); ++i)
                worst = juce::jmax (worst, std::abs (got.getSample (c, i) - expected.getSample (c, i)));
        return worst;
    }
}

TEST_CASE ("The supported formats include WAV, FLAC and MP3", "[formats]")
{
    CHECK (audioFormats::canOpen (juce::File ("a.wav")));
    CHECK (audioFormats::canOpen (juce::File ("a.WAV")));
    CHECK (audioFormats::canOpen (juce::File ("a.flac")));
    CHECK (audioFormats::canOpen (juce::File ("a.mp3")));
    CHECK (audioFormats::canOpen (juce::File ("a.aiff")));
    CHECK (audioFormats::canOpen (juce::File ("a.ogg")));
    CHECK_FALSE (audioFormats::canOpen (juce::File ("a.txt")));

    const auto wildcard = audioFormats::getWildcard();
    CHECK (wildcard.contains ("*.wav"));
    CHECK (wildcard.contains ("*.flac"));
    CHECK (wildcard.contains ("*.mp3"));
}

TEST_CASE ("A WAV file is read in place and its peaks are cached", "[formats]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto peakFolder = temp.folder.getChildFile ("peaks");
    const auto audio = makeSine (2, 100000, 48000.0, 440.0, 0.5f);
    const auto wav = writeFile<juce::WavAudioFormat> (temp.folder.getChildFile ("tone.wav"), audio, 48000.0, 24);

    auto first = importFile (wav, store, peakFolder);
    REQUIRE (first.result == AudioImporter::Result::ok);

    const auto& info = first.importer->getInfo();
    CHECK (info.isUncompressed);
    CHECK (info.numChannels == 2);
    CHECK (info.sampleRate == 48000.0);
    CHECK (info.numFrames == 100000);
    CHECK (info.bitsPerSample == 24);

    const auto& seq = first.importer->getSequence();
    CHECK (seq.getNumFrames() == 100000);
    CHECK (maxDifference (seq, audio) < 1.0e-5f); // 24 bit

    // Nothing was decoded into the block store.
    CHECK (store.getFolder().getNumberOfChildFiles (juce::File::findFiles) == 0);

    CHECK_FALSE (first.importer->usedCachedPeaks());
    CHECK (AudioImporter::getPeakFileFor (peakFolder, info).existsAsFile());

    auto second = importFile (wav, store, peakFolder);
    REQUIRE (second.result == AudioImporter::Result::ok);
    CHECK (second.importer->usedCachedPeaks());
    CHECK (second.importer->getPeaks()->isComplete());

    float lo, hi;
    REQUIRE (second.importer->getPeaks()->getMinMax (0, 0, 100000, lo, hi));
    CHECK_THAT (hi, Catch::Matchers::WithinAbs (0.5, 0.001));
    CHECK_THAT (lo, Catch::Matchers::WithinAbs (-0.5, 0.001));
}

TEST_CASE ("A FLAC file is decoded into the block store", "[formats]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto audio = makeSine (1, 70000, 44100.0, 1000.0, 0.8f);
    const auto flac = writeFile<juce::FlacAudioFormat> (temp.folder.getChildFile ("tone.flac"), audio, 44100.0, 16);

    auto imported = importFile (flac, store);
    REQUIRE (imported.result == AudioImporter::Result::ok);
    CHECK_FALSE (imported.importer->getInfo().isUncompressed);
    CHECK (imported.importer->getProgress() == 1.0);

    const auto& seq = imported.importer->getSequence();
    CHECK (seq.getNumChannels() == 1);
    CHECK (seq.getNumFrames() == 70000);
    CHECK (maxDifference (seq, audio) < 1.0e-4f); // 16 bit
    CHECK (store.getFolder().getNumberOfChildFiles (juce::File::findFiles) == 1);
}

TEST_CASE ("An MP3 file opens and sounds like the original", "[formats]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder);
    const auto mp3 = juce::File (MXJ_TEST_DATA_DIR).getChildFile ("sine440-stereo-1s.mp3");

    auto imported = importFile (mp3, store);
    REQUIRE (imported.result == AudioImporter::Result::ok);

    const auto& info = imported.importer->getInfo();
    CHECK (info.numChannels == 2);
    CHECK (info.sampleRate == 44100.0);
    CHECK (info.numFrames > 43000); // MP3 adds a little silence at both ends
    CHECK (info.numFrames < 47000);

    // The tone peaks at 0.084 (measured with FFmpeg on the same file).
    float lo, hi;
    REQUIRE (imported.importer->getPeaks()->getMinMax (0, 0, info.numFrames, lo, hi));
    CHECK_THAT (hi, Catch::Matchers::WithinAbs (0.084, 0.01));
    CHECK_THAT (lo, Catch::Matchers::WithinAbs (-0.084, 0.01));

    // About 440 cycles in one second: count rising zero crossings in the middle.
    juce::AudioBuffer<float> middle (1, 22050);
    REQUIRE (imported.importer->getSequence().read (middle.getArrayOfWritePointers(), 1, 11025, 22050));
    int crossings = 0;
    for (int i = 1; i < 22050; ++i)
        if (middle.getSample (0, i - 1) < 0.0f && middle.getSample (0, i) >= 0.0f)
            ++crossings;
    CHECK (crossings >= 218);
    CHECK (crossings <= 222);
}

TEST_CASE ("Broken and unknown files give a clear error", "[formats]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    juce::String error;

    const auto text = temp.folder.getChildFile ("notes.txt");
    text.replaceWithText ("hello");
    CHECK (AudioImporter::create (text, store, {}, error) == nullptr);
    CHECK (error.isNotEmpty());

    error.clear();
    const auto fake = temp.folder.getChildFile ("fake.wav");
    fake.replaceWithText ("this is not a wave file");
    CHECK (AudioImporter::create (fake, store, {}, error) == nullptr);
    CHECK (error.isNotEmpty());

    error.clear();
    CHECK (AudioImporter::create (temp.folder.getChildFile ("missing.flac"), store, {}, error) == nullptr);
    CHECK (error.isNotEmpty());
}

TEST_CASE ("An import can be cancelled", "[formats]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto audio = makeSine (2, 400000, 48000.0, 100.0, 0.5f);
    const auto flac = writeFile<juce::FlacAudioFormat> (temp.folder.getChildFile ("long.flac"), audio, 48000.0, 16);

    juce::String error;
    auto importer = AudioImporter::create (flac, store, {}, error);
    REQUIRE (importer != nullptr);

    int calls = 0;
    CHECK (importer->run ([&calls] { return ++calls < 3; }) == AudioImporter::Result::cancelled);
    CHECK (importer->getSequence().isEmpty());
    CHECK (store.getFolder().getNumberOfChildFiles (juce::File::findFiles) == 0);
}
