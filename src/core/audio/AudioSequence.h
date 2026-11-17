// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/SampleSource.h"

#include <vector>

namespace mxj
{
    /** One piece of an AudioSequence: a range of frames inside a SampleSource. */
    struct AudioBlock
    {
        SampleSourcePtr source;
        juce::int64 sourceStart = 0; ///< first frame inside the source
        juce::int64 length = 0;      ///< number of frames
    };

    /** The audio of one channel group (a mono or stereo file, a clip), as an
        ordered list of blocks.

        This is the heart of non-destructive editing (master plan, section 6):
        the audio data in the blocks never changes. Cutting and pasting only
        rearrange the list (phase 2), so undo is cheap and hours of audio are
        edited without loading them into memory.

        A sequence is a value: copying it copies the list, never the audio.
        All blocks must have the sequence's channel count and sample rate.
        Reading is thread safe but may touch the disk (not for the audio thread).
    */
    class AudioSequence
    {
    public:
        AudioSequence() = default;
        AudioSequence (int numChannels, double sampleRate);

        /** A sequence covering the whole source, split into blocks of at most
            `maxBlockFrames` frames. */
        static AudioSequence fromSource (SampleSourcePtr source, juce::int64 maxBlockFrames = defaultBlockFrames);

        /** Appends a block. Returns false (and adds nothing) if the source has a
            different channel count or sample rate, or the range lies outside it. */
        bool append (AudioBlock block);

        int getNumChannels() const noexcept           { return numChannels; }
        double getSampleRate() const noexcept         { return sampleRate; }
        juce::int64 getNumFrames() const noexcept     { return totalFrames; }
        double getLengthInSeconds() const noexcept    { return sampleRate > 0 ? (double) totalFrames / sampleRate : 0.0; }
        bool isEmpty() const noexcept                 { return totalFrames == 0; }
        const std::vector<AudioBlock>& getBlocks() const noexcept { return blocks; }

        /** Reads frames into `dest`. Missing channels and frames outside the
            sequence are silent. Returns false if a source failed to read. */
        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const;

        /** About one million frames: 4 MB per channel in 32-bit float. */
        static constexpr juce::int64 defaultBlockFrames = 1 << 20;

    private:
        /** Index of the block containing `frame`, or blocks.size(). */
        size_t findBlock (juce::int64 frame) const;

        int numChannels = 0;
        double sampleRate = 0.0;
        juce::int64 totalFrames = 0;
        std::vector<AudioBlock> blocks;
        std::vector<juce::int64> blockStarts; ///< start frame of each block in the sequence
    };
}
