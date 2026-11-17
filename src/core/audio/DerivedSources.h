// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/audio/AudioSequence.h"

namespace mxj
{
    /** Sources that compute their audio from other sequences when read.

        They make edits such as fades, silence and mix-paste instant and free of
        disk space, whatever the length of the audio, and they keep the original
        audio untouched (docs/adr/0006). Their inputs are sequences, which are
        values, so later edits elsewhere never change what they play.
        All are thread safe; read() allocates scratch memory (never use them on
        the audio thread, which only reads the playback FIFO).
    */

    /** Digital silence of a given length. */
    class SilenceSource final : public SampleSource
    {
    public:
        SilenceSource (int numChannels, double sampleRate, juce::int64 numFrames);

        int getNumChannels() const override         { return channels; }
        juce::int64 getNumFrames() const override   { return frames; }
        double getSampleRate() const override       { return rate; }

        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const override;
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const override;

    private:
        int channels;
        double rate;
        juce::int64 frames;
    };

    /** The input multiplied by a gain that moves from `startGain` to `endGain`
        over its length: fade in, fade out, or a constant gain. */
    class GainRampSource final : public SampleSource
    {
    public:
        enum class Curve
        {
            linear, ///< straight line
            sine    ///< quarter sine: natural sounding fades (equal power)
        };

        GainRampSource (AudioSequence input, float startGain, float endGain, Curve curve);

        int getNumChannels() const override         { return input.getNumChannels(); }
        juce::int64 getNumFrames() const override   { return input.getNumFrames(); }
        double getSampleRate() const override       { return input.getSampleRate(); }

        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const override;
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const override;

        /** The gain applied at one frame. */
        float getGainAt (juce::int64 frame) const noexcept;

        const AudioSequence& getInput() const noexcept { return input; }
        float getStartGain() const noexcept            { return startGain; }
        float getEndGain() const noexcept              { return endGain; }
        Curve getCurve() const noexcept                { return curve; }

    private:
        AudioSequence input;
        float startGain, endGain;
        Curve curve;
    };

    /** Two sequences of the same format added together. As long as the longer one. */
    class MixSource final : public SampleSource
    {
    public:
        MixSource (AudioSequence a, AudioSequence b);

        int getNumChannels() const override         { return a.getNumChannels(); }
        juce::int64 getNumFrames() const override   { return juce::jmax (a.getNumFrames(), b.getNumFrames()); }
        double getSampleRate() const override       { return a.getSampleRate(); }

        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const override;
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const override;

        const AudioSequence& getFirst() const noexcept  { return a; }
        const AudioSequence& getSecond() const noexcept { return b; }

    private:
        AudioSequence a, b;
    };

    /** The input with a different number of channels: mono is copied to every
        channel, several channels mixed to mono are averaged, and otherwise
        channel c plays input channel c (missing ones repeat the input). */
    class ChannelMapSource final : public SampleSource
    {
    public:
        ChannelMapSource (AudioSequence input, int numChannels);

        int getNumChannels() const override         { return channels; }
        juce::int64 getNumFrames() const override   { return input.getNumFrames(); }
        double getSampleRate() const override       { return input.getSampleRate(); }

        bool read (float* const* dest, int numDestChannels, juce::int64 startFrame, int numFrames) const override;
        bool getMinMax (int channel, juce::int64 startFrame, juce::int64 endFrame, float& minOut, float& maxOut) const override;

        const AudioSequence& getInput() const noexcept { return input; }

    private:
        AudioSequence input;
        int channels;
    };

    //==============================================================================
    /** Helpers that wrap a derived source into a sequence. */
    namespace derived
    {
        AudioSequence silence (int numChannels, double sampleRate, juce::int64 numFrames);
        AudioSequence gainRamp (const AudioSequence& input, float startGain, float endGain,
                                GainRampSource::Curve curve = GainRampSource::Curve::sine);
        AudioSequence mix (const AudioSequence& a, const AudioSequence& b);

        /** The input itself if it already has `numChannels` channels. */
        AudioSequence withChannels (const AudioSequence& input, int numChannels);
    }
}
