// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/AudioSequence.h"

#include <algorithm>
#include <limits>

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
        {
            // Not through append(): that would merge the pieces again.
            seq.blockStarts.push_back (seq.totalFrames);
            seq.blocks.push_back ({ source, start, juce::jmin (maxBlockFrames, total - start) });
            seq.totalFrames += seq.blocks.back().length;
        }

        return seq;
    }

    bool AudioSequence::hasSameFormat (const AudioSequence& other) const noexcept
    {
        return numChannels == other.numChannels && juce::approximatelyEqual (sampleRate, other.sampleRate);
    }

    bool AudioSequence::isSameAs (const AudioSequence& other) const noexcept
    {
        if (! hasSameFormat (other) || totalFrames != other.totalFrames || blocks.size() != other.blocks.size())
            return false;

        for (size_t i = 0; i < blocks.size(); ++i)
            if (blocks[i].source != other.blocks[i].source || blocks[i].sourceStart != other.blocks[i].sourceStart
                || blocks[i].length != other.blocks[i].length)
                return false;

        return true;
    }

    bool AudioSequence::append (AudioBlock block)
    {
        if (block.source == nullptr || block.length <= 0
            || block.source->getNumChannels() != numChannels
            || ! juce::approximatelyEqual (block.source->getSampleRate(), sampleRate)
            || block.sourceStart < 0
            || block.sourceStart + block.length > block.source->getNumFrames())
            return false;

        if (! blocks.empty())
        {
            auto& last = blocks.back();
            if (last.source == block.source
                && last.sourceStart + last.length == block.sourceStart
                && last.length + block.length <= defaultBlockFrames)
            {
                last.length += block.length;
                totalFrames += block.length;
                return true;
            }
        }

        blockStarts.push_back (totalFrames);
        totalFrames += block.length;
        blocks.push_back (std::move (block));
        return true;
    }

    bool AudioSequence::append (const AudioSequence& other)
    {
        if (other.isEmpty())
            return true;

        if (! hasSameFormat (other))
            return false;

        for (const auto& b : other.blocks)
            append (b);

        return true;
    }

    size_t AudioSequence::findBlock (juce::int64 frame) const
    {
        // Last block whose start is <= frame.
        const auto it = std::upper_bound (blockStarts.begin(), blockStarts.end(), frame);
        return it == blockStarts.begin() ? blocks.size() : (size_t) (it - blockStarts.begin() - 1);
    }

    void AudioSequence::rebuildStarts()
    {
        blockStarts.resize (blocks.size());
        totalFrames = 0;

        for (size_t i = 0; i < blocks.size(); ++i)
        {
            blockStarts[i] = totalFrames;
            totalFrames += blocks[i].length;
        }
    }

    size_t AudioSequence::splitAt (juce::int64 frame)
    {
        if (frame <= 0)
            return 0;

        if (frame >= totalFrames)
            return blocks.size();

        const auto index = findBlock (frame);
        const auto offset = frame - blockStarts[index];
        if (offset == 0)
            return index;

        auto second = blocks[index];
        second.sourceStart += offset;
        second.length -= offset;
        blocks[index].length = offset;
        blocks.insert (blocks.begin() + (std::ptrdiff_t) index + 1, std::move (second));
        blockStarts.insert (blockStarts.begin() + (std::ptrdiff_t) index + 1, frame);
        return index + 1;
    }

    //==============================================================================
    AudioSequence AudioSequence::getRange (juce::int64 start, juce::int64 length) const
    {
        AudioSequence result (numChannels, sampleRate);
        const auto range = juce::Range<juce::int64> (start, start + juce::jmax ((juce::int64) 0, length))
                               .getIntersectionWith ({ 0, totalFrames });
        if (range.isEmpty())
            return result;

        auto pos = range.getStart();
        for (auto index = findBlock (pos); pos < range.getEnd() && index < blocks.size(); ++index)
        {
            const auto& b = blocks[index];
            const auto offset = pos - blockStarts[index];
            const auto count = juce::jmin (b.length - offset, range.getEnd() - pos);
            result.blockStarts.push_back (result.totalFrames);
            result.blocks.push_back ({ b.source, b.sourceStart + offset, count });
            result.totalFrames += count;
            pos += count;
        }

        return result;
    }

    void AudioSequence::erase (juce::int64 start, juce::int64 length)
    {
        const auto range = juce::Range<juce::int64> (start, start + juce::jmax ((juce::int64) 0, length))
                               .getIntersectionWith ({ 0, totalFrames });
        if (range.isEmpty())
            return;

        const auto first = splitAt (range.getStart());
        const auto last = splitAt (range.getEnd());
        blocks.erase (blocks.begin() + (std::ptrdiff_t) first, blocks.begin() + (std::ptrdiff_t) last);
        rebuildStarts();
    }

    bool AudioSequence::insert (juce::int64 position, const AudioSequence& other)
    {
        if (other.isEmpty())
            return true;

        if (blocks.empty() && numChannels == 0)
        {
            *this = other;
            return true;
        }

        if (! hasSameFormat (other))
            return false;

        const auto index = splitAt (juce::jlimit ((juce::int64) 0, totalFrames, position));
        blocks.insert (blocks.begin() + (std::ptrdiff_t) index, other.blocks.begin(), other.blocks.end());
        rebuildStarts();
        return true;
    }

    bool AudioSequence::replace (juce::int64 start, juce::int64 length, const AudioSequence& other)
    {
        if (! other.isEmpty() && ! hasSameFormat (other) && ! (blocks.empty() && numChannels == 0))
            return false;

        erase (start, length);
        return insert (start, other);
    }

    //==============================================================================
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

    bool AudioSequence::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        startFrame = juce::jmax ((juce::int64) 0, startFrame);
        endFrame = juce::jmin (endFrame, totalFrames);
        if (startFrame >= endFrame || channel < 0 || channel >= numChannels)
            return false;

        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        bool any = false;
        auto pos = startFrame;

        for (auto index = findBlock (pos); pos < endFrame && index < blocks.size(); ++index)
        {
            const auto& b = blocks[index];
            const auto offset = pos - blockStarts[index];
            const auto count = juce::jmin (b.length - offset, endFrame - pos);
            float l, h;

            if (b.source->getMinMax (channel, b.sourceStart + offset, b.sourceStart + offset + count, l, h))
            {
                lo = juce::jmin (lo, l);
                hi = juce::jmax (hi, h);
                any = true;
            }

            pos += count;
        }

        if (any)
        {
            minOut = lo;
            maxOut = hi;
        }

        return any;
    }
}
