// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/SampleSource.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <mutex>

namespace mxj
{
    /** A SampleSource that reads straight from an uncompressed audio file
        (WAV, AIFF) on disk, so a one hour file opens without being copied.
        See docs/adr/0005 for why this is safe and what phase 2 must respect. */
    class FileSampleSource final : public SampleSource
    {
    public:
        /** Takes ownership of the reader. */
        FileSampleSource (juce::File file, std::unique_ptr<juce::AudioFormatReader> reader);

        int getNumChannels() const override         { return (int) reader->numChannels; }
        juce::int64 getNumFrames() const override   { return reader->lengthInSamples; }
        double getSampleRate() const override       { return reader->sampleRate; }

        bool read (float* const* dest, int numDestChannels,
                   juce::int64 startFrame, int numFrames) const override;

        juce::File getFile() const { return file; }

    private:
        juce::File file;
        std::unique_ptr<juce::AudioFormatReader> reader;
        mutable std::mutex readerLock;
    };
}
