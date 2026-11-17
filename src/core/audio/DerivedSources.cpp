// SPDX-License-Identifier: AGPL-3.0-only
#include "core/audio/DerivedSources.h"

#include <cmath>

namespace mxj
{
    namespace
    {
        /** Scratch audio for one read: allocated per call, so reads stay thread safe. */
        struct Scratch
        {
            Scratch (int channels, int frames) : buffer (juce::jmax (1, channels), juce::jmax (1, frames)) {}
            float* const* pointers() { return buffer.getArrayOfWritePointers(); }
            juce::AudioBuffer<float> buffer;
        };
    }

    //==============================================================================
    SilenceSource::SilenceSource (int numChannels, double sampleRate, juce::int64 numFrames)
        : channels (numChannels), rate (sampleRate), frames (juce::jmax ((juce::int64) 0, numFrames)) {}

    bool SilenceSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        clipReadRange (dest, numDestChannels, startFrame, numFrames, frames);
        return true;
    }

    bool SilenceSource::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        if (channel < 0 || channel >= channels || juce::jmax ((juce::int64) 0, startFrame) >= juce::jmin (endFrame, frames))
            return false;

        minOut = maxOut = 0.0f;
        return true;
    }

    //==============================================================================
    GainRampSource::GainRampSource (AudioSequence in, float start, float end, Curve c)
        : input (std::move (in)), startGain (start), endGain (end), curve (c) {}

    float GainRampSource::getGainAt (juce::int64 frame) const noexcept
    {
        const auto length = input.getNumFrames();
        if (length <= 1 || juce::approximatelyEqual (startGain, endGain))
            return endGain;

        frame = juce::jlimit ((juce::int64) 0, length - 1, frame);
        if (frame == 0)          return startGain;
        if (frame == length - 1) return endGain; // exact ends: a fade really reaches silence

        const auto t = (double) frame / (double) (length - 1);
        auto shape = t;

        if (curve == Curve::sine)
        {
            // Rising: sin; falling: the mirror image, so a fade out sounds like a reversed fade in.
            const auto halfPi = juce::MathConstants<double>::halfPi;
            shape = endGain > startGain ? std::sin (t * halfPi) : 1.0 - std::cos (t * halfPi);
        }

        return (float) (startGain + (endGain - startGain) * shape);
    }

    bool GainRampSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        const bool ok = input.read (dest, numDestChannels, startFrame, numFrames);

        const auto range = juce::Range<juce::int64> (startFrame, startFrame + numFrames).getIntersectionWith ({ 0, input.getNumFrames() });
        if (range.isEmpty())
            return ok;

        if (juce::approximatelyEqual (startGain, endGain))
        {
            for (int c = 0; c < numDestChannels; ++c)
                juce::FloatVectorOperations::multiply (dest[c] + (range.getStart() - startFrame), startGain, (int) range.getLength());
            return ok;
        }

        for (auto f = range.getStart(); f < range.getEnd(); ++f)
        {
            const auto g = getGainAt (f);
            const auto i = (int) (f - startFrame);
            for (int c = 0; c < numDestChannels; ++c)
                dest[c][i] *= g;
        }

        return ok;
    }

    bool GainRampSource::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        float lo, hi;
        if (! input.getMinMax (channel, startFrame, endFrame, lo, hi))
            return false;

        // The gain is monotonic, so its largest size is at one end of the range.
        const auto g = juce::jmax (std::abs (getGainAt (startFrame)), std::abs (getGainAt (endFrame - 1)));
        minOut = lo * g;
        maxOut = hi * g;
        return true;
    }

    //==============================================================================
    MixSource::MixSource (AudioSequence first, AudioSequence second)
        : a (std::move (first)), b (std::move (second))
    {
        jassert (a.hasSameFormat (b));
    }

    bool MixSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        bool ok = a.read (dest, numDestChannels, startFrame, numFrames);

        Scratch scratch (numDestChannels, numFrames);
        ok = b.read (scratch.pointers(), numDestChannels, startFrame, numFrames) && ok;

        for (int c = 0; c < numDestChannels; ++c)
            juce::FloatVectorOperations::add (dest[c], scratch.buffer.getReadPointer (c), numFrames);
        return ok;
    }

    bool MixSource::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        float la, ha, lb, hb;
        const bool hasA = a.getMinMax (channel, startFrame, endFrame, la, ha);
        const bool hasB = b.getMinMax (channel, startFrame, endFrame, lb, hb);

        if (hasA && hasB)
        {
            // An upper bound of the sum: good enough for drawing.
            minOut = juce::jmin (la, lb, la + lb);
            maxOut = juce::jmax (ha, hb, ha + hb);
            return true;
        }

        if (hasA) { minOut = la; maxOut = ha; return true; }
        if (hasB) { minOut = lb; maxOut = hb; return true; }
        return false;
    }

    //==============================================================================
    ChannelMapSource::ChannelMapSource (AudioSequence in, int numChannels)
        : input (std::move (in)), channels (juce::jmax (1, numChannels)) {}

    bool ChannelMapSource::read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const
    {
        const auto inChannels = input.getNumChannels();
        Scratch scratch (inChannels, numFrames);
        const bool ok = input.read (scratch.pointers(), inChannels, startFrame, numFrames);

        for (int c = 0; c < numDestChannels; ++c)
        {
            if (c >= channels)
            {
                juce::FloatVectorOperations::clear (dest[c], numFrames);
                continue;
            }

            if (channels == 1 && inChannels > 1)
            {
                juce::FloatVectorOperations::clear (dest[c], numFrames);
                for (int i = 0; i < inChannels; ++i)
                    juce::FloatVectorOperations::addWithMultiply (dest[c], scratch.buffer.getReadPointer (i),
                                                                  1.0f / (float) inChannels, numFrames);
            }
            else
            {
                juce::FloatVectorOperations::copy (dest[c], scratch.buffer.getReadPointer (c % inChannels), numFrames);
            }
        }

        return ok;
    }

    bool ChannelMapSource::getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const
    {
        const auto inChannels = input.getNumChannels();
        if (channel < 0 || channel >= channels || inChannels <= 0)
            return false;

        if (channels == 1 && inChannels > 1)
        {
            float loSum = 0, hiSum = 0;
            for (int i = 0; i < inChannels; ++i)
            {
                float lo, hi;
                if (! input.getMinMax (i, startFrame, endFrame, lo, hi))
                    return false;
                loSum += lo;
                hiSum += hi;
            }

            minOut = loSum / (float) inChannels;
            maxOut = hiSum / (float) inChannels;
            return true;
        }

        return input.getMinMax (channel % inChannels, startFrame, endFrame, minOut, maxOut);
    }

    //==============================================================================
    namespace derived
    {
        AudioSequence silence (int numChannels, double sampleRate, juce::int64 numFrames)
        {
            if (numFrames <= 0)
                return AudioSequence (numChannels, sampleRate);

            return AudioSequence::fromSource (std::make_shared<SilenceSource> (numChannels, sampleRate, numFrames),
                                              std::numeric_limits<juce::int64>::max());
        }

        AudioSequence gainRamp (const AudioSequence& input, float startGain, float endGain, GainRampSource::Curve curve)
        {
            if (input.isEmpty())
                return input;

            return AudioSequence::fromSource (std::make_shared<GainRampSource> (input, startGain, endGain, curve),
                                              std::numeric_limits<juce::int64>::max());
        }

        AudioSequence mix (const AudioSequence& a, const AudioSequence& b)
        {
            if (a.isEmpty()) return b;
            if (b.isEmpty()) return a;

            return AudioSequence::fromSource (std::make_shared<MixSource> (a, b), std::numeric_limits<juce::int64>::max());
        }

        AudioSequence withChannels (const AudioSequence& input, int numChannels)
        {
            if (input.getNumChannels() == numChannels)
                return input;

            if (input.isEmpty())
                return AudioSequence (numChannels, input.getSampleRate());

            return AudioSequence::fromSource (std::make_shared<ChannelMapSource> (input, numChannels),
                                              std::numeric_limits<juce::int64>::max());
        }
    }
}
