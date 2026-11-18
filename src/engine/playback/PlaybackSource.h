// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"

namespace mxj
{
    /** What the PlaybackEngine plays: a file (SequencePlaybackSource) or the
        mix of a multitrack session (SessionMixer).

        Read one chunk after the other on the playback engine's disk thread
        (never on the audio thread), so a source may touch the disk and keep
        state such as effect tails. Always delivers two channels (stereo).
    */
    class PlaybackSource
    {
    public:
        virtual ~PlaybackSource() = default;

        virtual double getSampleRate() const = 0;

        /** Frames that can be played. Any thread. */
        virtual juce::int64 getLength() const = 0;

        /** Prepares reading from `frame` (clears effect state). Disk thread. */
        virtual void seek (juce::int64 frame) = 0;

        /** Reads the next `numFrames` frames into two channels. Disk thread. */
        virtual void read (float* const* dest, int numFrames) = 0;
    };

    /** Plays an AudioSequence (the waveform editor). Mono plays on both sides;
        of more than two channels the first two play. */
    class SequencePlaybackSource final : public PlaybackSource
    {
    public:
        explicit SequencePlaybackSource (AudioSequence s) : sequence (std::move (s)) {}

        double getSampleRate() const override       { return sequence.getSampleRate(); }
        juce::int64 getLength() const override      { return sequence.getNumFrames(); }
        void seek (juce::int64 frame) override      { position = frame; }

        void read (float* const* dest, int numFrames) override
        {
            sequence.read (dest, 2, position, numFrames);
            if (sequence.getNumChannels() == 1)
                juce::FloatVectorOperations::copy (dest[1], dest[0], numFrames);

            position += numFrames;
        }

    private:
        AudioSequence sequence;
        juce::int64 position = 0;
    };
}
