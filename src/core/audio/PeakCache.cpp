// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/PeakCache.h"

#include <limits>

namespace mxj
{
    namespace
    {
        constexpr juce::uint32 fileMagic = 0x4b504a4d; // "MJPK"
        constexpr juce::uint32 fileVersion = 1;
        constexpr float emptyMin = std::numeric_limits<float>::max();
        constexpr float emptyMax = std::numeric_limits<float>::lowest();
    }

    PeakCache::PeakCache (int channels, juce::int64 frames)
        : numChannels (juce::jmax (1, channels)), totalFrames (juce::jmax ((juce::int64) 0, frames))
    {
        juce::int64 framesPerEntry = baseFrames;

        // Levels until one entry covers the whole file (always at least one level).
        do
        {
            Level level;
            level.framesPerEntry = framesPerEntry;
            const auto entries = (totalFrames + framesPerEntry - 1) / framesPerEntry;
            level.minMax.resize ((size_t) (entries * numChannels * 2));

            for (size_t i = 0; i < level.minMax.size(); i += 2)
            {
                level.minMax[i] = emptyMin;
                level.minMax[i + 1] = emptyMax;
            }

            levels.push_back (std::move (level));
            framesPerEntry *= levelFactor;
        }
        while (framesPerEntry / levelFactor < totalFrames);
    }

    void PeakCache::append (const float* const* channelData, int numFrames)
    {
        numFrames = (int) juce::jmin ((juce::int64) numFrames, totalFrames - framesWritten);
        if (numFrames <= 0)
            return;

        auto& base = levels[0];

        for (int c = 0; c < numChannels; ++c)
        {
            const auto* data = channelData[c];
            auto frame = framesWritten;
            int i = 0;

            while (i < numFrames)
            {
                const auto entry = frame / baseFrames;
                const auto count = (int) juce::jmin ((juce::int64) (numFrames - i), (entry + 1) * baseFrames - frame);
                const auto range = juce::FloatVectorOperations::findMinAndMax (data + i, count);
                auto* mm = &base.minMax[(size_t) ((entry * numChannels + c) * 2)];
                mm[0] = juce::jmin (mm[0], range.getStart());
                mm[1] = juce::jmax (mm[1], range.getEnd());
                i += count;
                frame += count;
            }
        }

        framesWritten += numFrames;
        const bool done = framesWritten >= totalFrames;
        updateLevelsUpTo (framesWritten, done);
        framesReady.store (framesWritten, std::memory_order_release);

        if (done)
            complete.store (true, std::memory_order_release);
    }

    void PeakCache::finishEarly()
    {
        updateLevelsUpTo (framesWritten, true);
        framesReady.store (framesWritten, std::memory_order_release);
        complete.store (true, std::memory_order_release);
    }

    void PeakCache::updateLevelsUpTo (juce::int64 frames, bool includePartial)
    {
        // Entries of level 0 that are complete (or the last, partial one at the end).
        auto completeEntries = [&] (const Level& l)
        {
            return includePartial ? (frames + l.framesPerEntry - 1) / l.framesPerEntry
                                  : frames / l.framesPerEntry;
        };

        levels[0].entriesDone = completeEntries (levels[0]);

        for (size_t li = 1; li < levels.size(); ++li)
        {
            auto& lower = levels[li - 1];
            auto& level = levels[li];
            const auto target = completeEntries (level);

            for (auto e = level.entriesDone; e < target; ++e)
            {
                const auto firstChild = e * levelFactor;
                const auto lastChild = juce::jmin (firstChild + levelFactor, lower.entriesDone);

                for (int c = 0; c < numChannels; ++c)
                {
                    float lo = emptyMin, hi = emptyMax;
                    for (auto child = firstChild; child < lastChild; ++child)
                    {
                        const auto* mm = &lower.minMax[(size_t) ((child * numChannels + c) * 2)];
                        lo = juce::jmin (lo, mm[0]);
                        hi = juce::jmax (hi, mm[1]);
                    }

                    auto* out = &level.minMax[(size_t) ((e * numChannels + c) * 2)];
                    out[0] = lo;
                    out[1] = hi;
                }
            }

            level.entriesDone = target;
        }
    }

    bool PeakCache::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        const bool done = isComplete(); // read first: when true, framesReady is final
        const auto ready = getFramesReady();
        startFrame = juce::jmax ((juce::int64) 0, startFrame);
        endFrame = juce::jmin (endFrame, ready);

        if (channel < 0 || channel >= numChannels || startFrame >= endFrame)
            return false;

        // Coarsest level with at least two entries across the range, so the
        // result stays close to the exact range while reading few entries.
        const auto span = endFrame - startFrame;
        size_t li = 0;
        while (li + 1 < levels.size() && levels[li + 1].framesPerEntry * 2 <= span)
            ++li;

        const auto& level = levels[li];
        const auto firstEntry = startFrame / level.framesPerEntry;
        const auto lastEntry = (endFrame - 1) / level.framesPerEntry; // inclusive
        const auto readyEntries = done ? (ready + level.framesPerEntry - 1) / level.framesPerEntry
                                       : ready / level.framesPerEntry;

        float lo = emptyMin, hi = emptyMax;
        for (auto e = firstEntry; e <= lastEntry && e < readyEntries; ++e)
        {
            const auto* mm = &level.minMax[(size_t) ((e * numChannels + channel) * 2)];
            lo = juce::jmin (lo, mm[0]);
            hi = juce::jmax (hi, mm[1]);
        }

        if (lo > hi)
            return false; // only an unfinished entry lies in the range

        minOut = lo;
        maxOut = hi;
        return true;
    }

    bool PeakCache::save (const juce::File& file) const
    {
        if (! isComplete() || getFramesReady() != totalFrames)
            return false;

        const auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
        {
            juce::FileOutputStream out (temp);
            if (out.failedToOpen())
                return false;

            out.setPosition (0);
            out.truncate();
            out.writeInt ((int) fileMagic);
            out.writeInt ((int) fileVersion);
            out.writeInt (numChannels);
            out.writeInt64 (totalFrames);
            out.writeInt (baseFrames);

            const auto& base = levels[0].minMax;
            out.write (base.data(), base.size() * sizeof (float));
            out.flush();

            if (out.getStatus().failed())
                return false;
        }

        return temp.moveFileTo (file);
    }

    std::unique_ptr<PeakCache> PeakCache::load (const juce::File& file, int expectedChannels, juce::int64 expectedFrames)
    {
        juce::FileInputStream in (file);
        if (in.failedToOpen())
            return nullptr;

        if ((juce::uint32) in.readInt() != fileMagic || (juce::uint32) in.readInt() != fileVersion
            || in.readInt() != expectedChannels || in.readInt64() != expectedFrames || in.readInt() != baseFrames)
            return nullptr;

        auto cache = std::make_unique<PeakCache> (expectedChannels, expectedFrames);
        auto& base = cache->levels[0].minMax;
        const auto bytes = (int) (base.size() * sizeof (float));

        if (in.read (base.data(), bytes) != bytes)
            return nullptr;

        cache->framesWritten = expectedFrames;
        cache->updateLevelsUpTo (expectedFrames, true);
        cache->framesReady.store (expectedFrames, std::memory_order_release);
        cache->complete.store (true, std::memory_order_release);
        return cache;
    }
}
