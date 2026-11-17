// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/AudioSequence.h"

#include <algorithm>

namespace mxj
{
    AudioSequence::AudioSequence (int channels, double rate)
        : numChannels (channels), sampleRate (rate) {}

    AudioSequence AudioSequence::fromSource (SampleSourcePtr source, juce::int64 maxBlockFrames)
    {
        if (source == nullptr)
            return {};

        AudioSequence seq (source->getNumChannels(), source->getSampleRate());
        const auto total = source->getNumFrames();
        maxBlockFrames = juce::jmax ((juce::int64) 1, maxBlockFrames);

        for (juce::int64 start = 0; start < total; start += maxBlockFrames)
            seq.append ({ source, start, juce::jmin (maxBlockFrames, total - start) });

        return seq;
    }

    bool AudioSequence::append (AudioBlock block)
    {
        if (block.source == nullptr || block.length <= 0
            || block.source->getNumChannels() != numChannels
            || ! juce::approximatelyEqual (block.source->getSampleRate(), sampleRate)
            || block.sourceStart < 0
            || block.sourceStart + block.length > block.source->getNumFrames())
            return false;

        blockStarts.push_back (totalFrames);
        totalFrames += block.length;
        blocks.push_back (std::move (block));
        return true;
    }

    size_t AudioSequence::findBlock (juce::int64 frame) const
    {
        // Last block whose start is <= frame.
        const auto it = std::upper_bound (blockStarts.begin(), blockStarts.end(), frame);
        return it == blockStarts.begin() ? blocks.size() : (size_t) (it - blockStarts.begin() - 1);
    }

    bool AudioSequence::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        const auto range = clipReadRange (dest, numDestChannels, startFrame, numFrames, totalFrames);
        if (range.isEmpty())
            return true;

        bool ok = true;
        auto pos = range.getStart();
        auto index = findBlock (pos);
        float* channelPointers[64];
        const int channels = juce::jmin (numDestChannels, 64);

        while (pos < range.getEnd() && index < blocks.size())
        {
            const auto& b = blocks[index];
            const auto offsetInBlock = pos - blockStarts[index];
            const auto count = (int) juce::jmin (b.length - offsetInBlock, range.getEnd() - pos);
            const auto destOffset = (int) (pos - startFrame);

            for (int c = 0; c < channels; ++c)
                channelPointers[c] = dest[c] + destOffset;

            ok = b.source->read (channelPointers, channels, b.sourceStart + offsetInBlock, count) && ok;
            pos += count;
            ++index;
        }

        return ok;
    }
}
