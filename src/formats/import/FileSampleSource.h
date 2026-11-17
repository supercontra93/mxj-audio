// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/SampleSource.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
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
        ~FileSampleSource() override;

        int getNumChannels() const override         { return channels; }
        juce::int64 getNumFrames() const override   { return frames; }
        double getSampleRate() const override       { return rate; }

        bool read (float* const* dest, int numDestChannels,
                   juce::int64 startFrame, int numFrames) const override;

        /** The file read now (a copy after relocate()). */
        juce::File getFile() const;

        /** Makes the source read from `copy`, an exact byte copy of its file,
            so the original can be overwritten or deleted (saving over the
            open file; docs/adr/0006). Every sequence, clipboard and undo step
            that uses this source follows automatically. Returns false (and
            changes nothing) if the copy cannot be opened. */
        bool relocate (const juce::File& copy);

        /** Before `file` is overwritten or deleted: copies it once to the file
            `makeCopyName` returns and makes every live source that reads it read
            the copy instead. Returns false if a copy was needed but failed (then
            the file must not be overwritten). Thread safe. */
        static bool relocateAllReading (const juce::File& file, const std::function<juce::File()>& makeCopyName);

        /** {"type": "file", "path", "size", "modified"}. */
        juce::var describe() const override;

    private:
        juce::File file;
        std::unique_ptr<juce::AudioFormatReader> reader;
        mutable std::mutex readerLock;
        int channels;
        juce::int64 frames;
        double rate;
    };
}
