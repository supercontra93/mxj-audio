// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/SampleSource.h"

namespace mxj
{
    juce::Range<juce::int64> clipReadRange (float* const* dest, int numDestChannels,
                                            juce::int64 startFrame, int numFrames, juce::int64 length)
    {
        for (int c = 0; c < numDestChannels; ++c)
            juce::FloatVectorOperations::clear (dest[c], numFrames);

        return juce::Range<juce::int64> (startFrame, startFrame + numFrames)
                   .getIntersectionWith ({ 0, length });
    }

    bool SampleSource::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame,
                                  float& minOut, float& maxOut) const
    {
        return peaks != nullptr && peaks->getMinMax (channel, startFrame, endFrame, minOut, maxOut);
    }

    MemorySampleSource::MemorySampleSource (juce::AudioBuffer<float> data, double sampleRate)
        : buffer (std::move (data)), rate (sampleRate)
    {
        auto p = std::make_shared<PeakCache> (buffer.getNumChannels(), buffer.getNumSamples());
        if (buffer.getNumSamples() > 0)
            p->append (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
        else
            p->finishEarly();
        setPeaks (std::move (p));
    }

    bool MemorySampleSource::read (float* const* dest, int numDestChannels,
                                   juce::int64 startFrame, int numFrames) const
    {
        const auto range = clipReadRange (dest, numDestChannels, startFrame, numFrames, getNumFrames());
        if (range.isEmpty())
            return true;

        const int offset = (int) (range.getStart() - startFrame);
        for (int c = 0; c < juce::jmin (numDestChannels, buffer.getNumChannels()); ++c)
            juce::FloatVectorOperations::copy (dest[c] + offset,
                                               buffer.getReadPointer (c, (int) range.getStart()),
                                               (int) range.getLength());
        return true;
    }
}
