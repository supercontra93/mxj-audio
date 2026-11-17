// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "core/edit/EditOperations.h"
#include "dsp/convert/Resample.h"
#include "formats/export/AudioExporter.h"
#include "formats/import/AudioImporter.h"
#include "formats/import/FileSampleSource.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace mxj;

namespace
{
    AudioSequence makeSine (int channels, int frames, double rate, double frequency = 440.0, float gain = 0.5f)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                b.setSample (c, i, gain * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate + c));
        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate));
    }

    juce::AudioBuffer<float> readAll (const AudioSequence& seq)
    {
        juce::AudioBuffer<float> b (seq.getNumChannels(), (int) seq.getNumFrames());
        seq.read (b.getArrayOfWritePointers(), b.getNumChannels(), 0, b.getNumSamples());
        return b;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int offsetInB = 0)
    {
        float worst = 0;
        for (int c = 0; c < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++c)
            for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples() - offsetInB); ++i)
                worst = juce::jmax (worst, std::abs (a.getSample (c, i) - b.getSample (c, i + offsetInB)));
        return worst;
    }

    AudioSequence import (const juce::File& file, BlockStore& store)
    {
        juce::String error;
        auto importer = AudioImporter::create (file, store, {}, error);
        INFO (error);
        REQUIRE (importer != nullptr);
        REQUIRE (importer->run (nullptr) == AudioImporter::Result::ok);
        return importer->getSequence();
    }

    audioExport::Result save (const AudioSequence& audio, const ExportSettings& settings)
    {
        juce::String error;
        const auto r = audioExport::write (audio, settings, nullptr, nullptr, error);
        INFO (error);
        return r;
    }
}

TEST_CASE ("WAV export keeps the audio at every bit depth", "[export]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto audio = makeSine (2, 48000, 48000.0);

    for (auto [bits, isFloat, tolerance] : { std::tuple { 16, false, 1.0e-4f }, { 24, false, 1.0e-6f }, { 32, true, 0.0f } })
    {
        ExportSettings s;
        s.file = temp.folder.getChildFile ("out" + juce::String (bits) + ".wav");
        s.bitsPerSample = bits;
        s.floatingPoint = isFloat;

        REQUIRE (save (audio, s) == audioExport::Result::ok);

        AudioFileInfo info;
        juce::String error;
        REQUIRE (audioFormats::readInfo (s.file, info, error));
        CHECK (info.bitsPerSample == bits);
        CHECK (info.usesFloat == isFloat);
        CHECK (info.numFrames == 48000);
        CHECK (maxDifference (readAll (audio), readAll (import (s.file, store))) <= tolerance);
    }
}

TEST_CASE ("Markers are saved in WAV files as cue points and read back", "[export][markers]")
{
    test::TempFolder temp;
    ExportSettings s;
    s.file = temp.folder.getChildFile ("markers.wav");
    s.markers = { { 0, "Start" }, { 12000, juce::String (juce::CharPointer_UTF8 ("Bölüm 2")) }, { 47999, "End" } };

    REQUIRE (save (makeSine (1, 48000, 48000.0), s) == audioExport::Result::ok);

    AudioFileInfo info;
    juce::String error;
    REQUIRE (audioFormats::readInfo (s.file, info, error));
    CHECK (info.markers == s.markers);
}

TEST_CASE ("FLAC, Ogg Vorbis, Opus and AIFF files can be written", "[export]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto audio = makeSine (2, 48000, 48000.0);

    for (auto format : { ExportFormat::flac, ExportFormat::ogg, ExportFormat::opus, ExportFormat::aiff })
    {
        ExportSettings s;
        s.format = format;
        s.bitsPerSample = 16;
        s.file = temp.folder.getChildFile ("out." + audioExport::getExtension (format));
        INFO (audioExport::getDisplayName (format));
        REQUIRE (audioExport::formatForFile (s.file) == format);
        REQUIRE (save (audio, s) == audioExport::Result::ok);

        const auto back = import (s.file, store);
        CHECK (back.getNumChannels() == 2);
        CHECK (std::abs (back.getNumFrames() - 48000) < 2000); // lossy codecs may pad a little

        if (format == ExportFormat::flac || format == ExportFormat::aiff)
            CHECK (maxDifference (readAll (audio), readAll (back)) < 1.0e-4f);
    }
}

TEST_CASE ("A failed save leaves the existing file alone", "[export]")
{
    test::TempFolder temp;
    const auto file = temp.folder.getChildFile ("keep.aiff");
    file.replaceWithText ("precious");

    ExportSettings s;
    s.file = file;
    s.format = ExportFormat::aiff;
    s.bitsPerSample = 16;

    // AIFF cannot store 7 kHz audio.
    CHECK (save (makeSine (1, 100, 7000.0), s) == audioExport::Result::failed);
    CHECK (file.loadFileAsString() == "precious");
    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFiles) == 1);

    // A cancelled save neither.
    juce::String error;
    CHECK (audioExport::write (makeSine (1, 200000, 48000.0), s, [] { return false; }, nullptr, error)
           == audioExport::Result::cancelled);
    CHECK (file.loadFileAsString() == "precious");
}

TEST_CASE ("Saving over the open WAV file keeps every edit and undo step playable", "[export][save]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto file = temp.folder.getChildFile ("song.wav");

    ExportSettings s;
    s.file = file;
    s.bitsPerSample = 32;
    s.floatingPoint = true;
    const auto original = makeSine (1, 96000, 48000.0);
    REQUIRE (save (original, s) == audioExport::Result::ok);

    // Opened in place: the blocks read the file itself.
    const auto opened = import (file, store);
    REQUIRE (dynamic_cast<const FileSampleSource*> (opened.getBlocks()[0].source.get()) != nullptr);

    const AudioState before { opened, {} };
    const auto edited = edits::erase (before, { 0, 48000 }).state; // the "undo step" keeps `before`

    // What Workspace::save does: move readers to a copy, then write over the file.
    REQUIRE (FileSampleSource::relocateAllReading (file, [&] { return store.createFileName (".wav"); }));
    REQUIRE (save (edited.audio, s) == audioExport::Result::ok);

    CHECK (import (file, store).getNumFrames() == 48000);
    CHECK (maxDifference (readAll (before.audio), readAll (original)) == 0.0f); // undo still has the old audio
    CHECK (maxDifference (readAll (edited.audio), readAll (original), 48000) == 0.0f);
}

TEST_CASE ("Sample rate conversion keeps length, pitch and timing", "[dsp][resample]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));
    const auto input = makeSine (1, 44100, 44100.0, 1000.0, 0.5f);

    const auto out = dsp::resample (input, 48000.0, store);
    REQUIRE (out.getSampleRate() == 48000.0);
    REQUIRE (out.getNumFrames() == 48000);

    // Away from the edges it matches a 1 kHz sine made at 48 kHz, in time.
    const auto expected = readAll (makeSine (1, 48000, 48000.0, 1000.0, 0.5f));
    const auto got = readAll (out);
    float worst = 0;
    for (int i = 1000; i < 47000; ++i)
        worst = juce::jmax (worst, std::abs (got.getSample (0, i) - expected.getSample (0, i)));
    CHECK (worst < 0.02f);

    // Peaks come with it, for drawing.
    float lo, hi;
    REQUIRE (out.getMinMax (0, 0, 48000, lo, hi));
    CHECK_THAT (hi, Catch::Matchers::WithinAbs (0.5, 0.02));
}
