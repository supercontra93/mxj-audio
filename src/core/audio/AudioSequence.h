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
        rearrange the list, so undo is cheap and hours of audio are edited
        without loading them into memory. Every edit costs time in proportion to
        the number of blocks, never to the length of the audio.

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
            different channel count or sample rate, or the range lies outside it.
            A block that continues the previous one in the same source is merged
            with it (up to defaultBlockFrames). */
        bool append (AudioBlock block);

        /** Appends all blocks of another sequence. Returns false (and changes
            nothing) if the format differs. */
        bool append (const AudioSequence& other);

        int getNumChannels() const noexcept           { return numChannels; }
        double getSampleRate() const noexcept         { return sampleRate; }
        juce::int64 getNumFrames() const noexcept     { return totalFrames; }
        double getLengthInSeconds() const noexcept    { return sampleRate > 0 ? (double) totalFrames / sampleRate : 0.0; }
        bool isEmpty() const noexcept                 { return totalFrames == 0; }
        const std::vector<AudioBlock>& getBlocks() const noexcept { return blocks; }

        /** True if both are made of exactly the same blocks (same audio, cheaply). */
        bool isSameAs (const AudioSequence& other) const noexcept;

        /** True if both have the same channel count and sample rate. */
        bool hasSameFormat (const AudioSequence& other) const noexcept;

        //==============================================================================
        /** The frames [start, start + length) as a new sequence (limited to this one). */
        AudioSequence getRange (juce::int64 start, juce::int64 length) const;

        /** Removes the frames [start, start + length). */
        void erase (juce::int64 start, juce::int64 length);

        /** Inserts another sequence before `position` (clamped to the length).
            Returns false (and changes nothing) if the format differs. */
        bool insert (juce::int64 position, const AudioSequence& other);

        /** Replaces the frames [start, start + length) by another sequence.
            Returns false (and changes nothing) if the format differs. */
        bool replace (juce::int64 start, juce::int64 length, const AudioSequence& other);

        //==============================================================================
        /** Reads frames into `dest`. Missing channels and frames outside the
            sequence are silent. Returns false if a source failed to read. */
        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const;

        /** Lowest and highest sample of one channel in [startFrame, endFrame), from
            the peaks of the sources. Returns false if nothing is known about it. */
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const;

        /** About one million frames: 4 MB per channel in 32-bit float. */
        static constexpr juce::int64 defaultBlockFrames = 1 << 20;

    private:
        /** Index of the block containing `frame`, or blocks.size(). */
        size_t findBlock (juce::int64 frame) const;

        /** Splits the block containing `frame` so that a block starts there.
            Returns the index of that block (blocks.size() at the end). */
        size_t splitAt (juce::int64 frame);

        void rebuildStarts();

        int numChannels = 0;
        double sampleRate = 0.0;
        juce::int64 totalFrames = 0;
        std::vector<AudioBlock> blocks;
        std::vector<juce::int64> blockStarts; ///< start frame of each block in the sequence
    };
}
