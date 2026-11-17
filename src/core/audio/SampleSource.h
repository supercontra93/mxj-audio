// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace mxj
{
    /** Immutable audio data: a fixed number of channels and frames that never change.

        Blocks of an AudioSequence point into sources. A source is either a file
        written by the BlockStore (32-bit float) or, for uncompressed files such as
        WAV, the original file itself (see docs/adr/0005).

        read() is thread safe, but it may touch the disk: never call it on the
        audio thread. The playback engine reads on its own disk thread.
    */
    class SampleSource
    {
    public:
        virtual ~SampleSource() = default;

        virtual int getNumChannels() const = 0;
        virtual juce::int64 getNumFrames() const = 0;
        virtual double getSampleRate() const = 0;

        /** Reads `numFrames` frames starting at `startFrame` into `dest`
            (one pointer per channel, `numDestChannels` of them).
            Channels the source does not have, and frames outside the source,
            are filled with silence. Returns false on a read error (the
            destination is then silent). */
        virtual bool read (float* const* dest, int numDestChannels,
                           juce::int64 startFrame, int numFrames) const = 0;
    };

    using SampleSourcePtr = std::shared_ptr<const SampleSource>;

    /** A source held in memory. Used for tests and for short generated audio. */
    class MemorySampleSource final : public SampleSource
    {
    public:
        MemorySampleSource (juce::AudioBuffer<float> data, double sampleRate);

        int getNumChannels() const override         { return buffer.getNumChannels(); }
        juce::int64 getNumFrames() const override   { return buffer.getNumSamples(); }
        double getSampleRate() const override       { return rate; }

        bool read (float* const* dest, int numDestChannels,
                   juce::int64 startFrame, int numFrames) const override;

    private:
        juce::AudioBuffer<float> buffer;
        double rate;
    };

    /** Helper for sources: zero-fills `dest` and returns the part of
        [startFrame, startFrame + numFrames) that lies inside [0, length). */
    juce::Range<juce::int64> clipReadRange (float* const* dest, int numDestChannels,
                                            juce::int64 startFrame, int numFrames,
                                            juce::int64 length);
}
