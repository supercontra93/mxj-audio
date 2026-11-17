// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <vector>

namespace mxj
{
    /** Multi-resolution minimum and maximum values of an audio sequence, used to
        draw the waveform quickly at any zoom level (master plan: "tepe önbelleği").

        Level 0 holds one min/max pair per `baseFrames` frames, each higher level
        one pair per `levelFactor` entries of the level below. One hour of stereo
        audio at 48 kHz needs about 25 MB.

        Built by one writer thread with append(), while any number of reader
        threads draw the part that is already done (getFramesReady()). All memory
        is reserved in the constructor, so readers never see it move.
    */
    class PeakCache
    {
    public:
        static constexpr int baseFrames = 128;
        static constexpr int levelFactor = 8;

        PeakCache (int numChannels, juce::int64 totalFrames);

        int getNumChannels() const noexcept            { return numChannels; }
        juce::int64 getTotalFrames() const noexcept    { return totalFrames; }

        /** Frames whose peaks can be read. Grows while the cache is built. */
        juce::int64 getFramesReady() const noexcept    { return framesReady.load (std::memory_order_acquire); }
        /** True once all audio has been added (or finishEarly() was called). */
        bool isComplete() const noexcept               { return complete.load (std::memory_order_acquire); }

        /** Writer only: adds the next frames (one pointer per channel). Frames
            beyond getTotalFrames() are ignored. */
        void append (const float* const* channelData, int numFrames);

        /** Writer only: ends a cache whose audio turned out to be shorter than
            announced. getFramesReady() then gives the real length. */
        void finishEarly();

        /** Lowest and highest sample of one channel in [startFrame, endFrame),
            limited to the frames that are ready. Uses the coarsest level that is
            still finer than the range, so the cost stays small at every zoom.
            Returns false if no ready frame lies in the range. */
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const;

        /** Writes the finished cache to a file. Returns false on failure. */
        bool save (const juce::File& file) const;

        /** Reads a cache written by save(), or returns nullptr if the file is
            missing, damaged or was made for different audio. */
        static std::unique_ptr<PeakCache> load (const juce::File& file, int expectedChannels, juce::int64 expectedFrames);

    private:
        struct Level
        {
            juce::int64 framesPerEntry = 0;
            std::vector<float> minMax; ///< per entry: channel 0 min, channel 0 max, channel 1 min, ...
            juce::int64 entriesDone = 0;
        };

        void updateLevelsUpTo (juce::int64 frames, bool includePartial);

        int numChannels;
        juce::int64 totalFrames;
        std::vector<Level> levels;
        std::atomic<juce::int64> framesReady { 0 };
        std::atomic<bool> complete { false };

        // Writer state: the partly filled level 0 entry.
        juce::int64 framesWritten = 0;

        JUCE_DECLARE_NON_COPYABLE (PeakCache)
    };
}
