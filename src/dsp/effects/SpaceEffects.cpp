// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/BuiltInEffects.h"
#include "dsp/filters/Biquad.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace mxj::dsp::builtin
{
    namespace
    {
        constexpr int maxChannels = 8;
        constexpr double twoPi = 6.283185307179586;

        template <typename Processor>
        EffectType withProcessor (EffectType t)
        {
            t.create = [] (ParameterValuesPtr v) { return std::make_unique<Processor> (std::move (v)); };
            return t;
        }

        /** A delay line with fractional (linear interpolated) reads. */
        class DelayLine
        {
        public:
            void setMaximumFrames (int frames)
            {
                buffer.assign ((size_t) juce::jmax (4, frames + 2), 0.0f);
                pos = 0;
            }

            void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

            void push (float x) noexcept
            {
                pos = pos + 1 < (int) buffer.size() ? pos + 1 : 0;
                buffer[(size_t) pos] = x;
            }

            /** The value pushed `delayFrames` pushes ago (0 = the last one). */
            float read (double delayFrames) const noexcept
            {
                const auto n = (int) buffer.size();
                delayFrames = juce::jlimit (0.0, (double) (n - 2), delayFrames);
                const auto whole = (int) delayFrames;
                const auto frac = (float) (delayFrames - whole);
                auto i0 = pos - whole;
                if (i0 < 0) i0 += n;
                auto i1 = i0 - 1;
                if (i1 < 0) i1 += n;
                return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
            }

        private:
            std::vector<float> buffer;
            int pos = 0;
        };
    }

    //==============================================================================
    namespace dly
    {
        enum { time, feedback, mix, highCut, pingPong };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                for (auto& d : lines)
                    d.setMaximumFrames ((int) (rate * 2.1));
                reset();
            }

            void reset() override
            {
                for (auto& d : lines)
                    d.clear();
                for (auto& f : damping)
                    f.reset();
                currentDelay = -1.0;
                lastHighCut = -1.0f;
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);
                const auto targetDelay = rate * param (time) * 0.001;
                if (currentDelay < 0.0)
                    currentDelay = targetDelay;

                if (param (highCut) != lastHighCut)
                {
                    lastHighCut = param (highCut);
                    const auto k = BiquadCoefficients::lowPass1 (rate, lastHighCut);
                    for (auto& f : damping)
                        f.setCoefficients (k);
                }

                const auto fb = juce::jlimit (0.0f, 0.95f, param (feedback) / 100.0f);
                const auto wet = juce::jlimit (0.0f, 1.0f, param (mix) / 100.0f);
                const bool cross = param (pingPong) > 0.5f && numChannels == 2;
                const auto glide = 1.0 - std::exp (-1.0 / (rate * 0.05)); // 50 ms glide when the time changes

                for (int i = 0; i < numFrames; ++i)
                {
                    currentDelay += (targetDelay - currentDelay) * glide;
                    std::array<float, maxChannels> echoes {};

                    for (int c = 0; c < numChannels; ++c)
                        echoes[(size_t) c] = lines[(size_t) c].read (currentDelay - 1.0);

                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto x = data[c][i];
                        const auto back = cross ? echoes[(size_t) (1 - c)] : echoes[(size_t) c];
                        const auto in = cross ? (c == 0 ? (x + data[1][i]) * 0.5f : 0.0f) : x;
                        lines[(size_t) c].push (damping[(size_t) c].processSample (in + back * fb));
                        data[c][i] = x * (1.0f - wet * 0.5f) + echoes[(size_t) c] * wet;
                    }
                }

                for (auto& f : damping)
                    f.sanitise();
            }

        private:
            double rate = 48000.0, currentDelay = -1.0;
            int channels = 2;
            float lastHighCut = -1.0f;
            std::array<DelayLine, maxChannels> lines;
            std::array<Biquad, maxChannels> damping {};
        };
    }

    EffectType delay()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "delay";
        t.name = "Delay (Echo)";
        t.category = "Delay and Reverb";
        t.description = "Repeat the sound after a short time";
        t.parameters = {
            P::number ("time", "Time", 1.0f, 2000.0f, 300.0f, "ms", 0, 300.0f),
            P::number ("feedback", "Feedback", 0.0f, 95.0f, 35.0f, "%", 0),
            P::number ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%", 0),
            P::number ("highCut", "Echo high cut", 1000.0f, 20000.0f, 8000.0f, "Hz", 0, 5000.0f),
            P::toggle ("pingPong", "Ping-pong (left/right)", false),
        };
        t.factoryPresets = {
            { "Slapback",       { 110, 10, 30, 9000, 0 } },
            { "Quarter note at 120 BPM", { 500, 40, 30, 7000, 0 } },
            { "Ping-pong",      { 375, 45, 35, 6000, 1 } },
            { "Long echo",      { 900, 55, 35, 4000, 0 } },
        };
        return withProcessor<dly::Processor> (std::move (t));
    }

    //==============================================================================
    // Algorithmic reverb: JUCE's Freeverb based juce::Reverb (part of JUCE, AGPLv3).
    namespace rev
    {
        enum { roomSize, damping, width, wet, dry, preDelay };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                reverb.setSampleRate (sampleRate);
                for (auto& d : preDelays)
                    d.setMaximumFrames ((int) (rate * 0.21));
                reset();
            }

            void reset() override
            {
                reverb.reset();
                for (auto& d : preDelays)
                    d.clear();
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);

                juce::Reverb::Parameters p;
                p.roomSize = juce::jlimit (0.0f, 1.0f, param (roomSize) / 100.0f);
                p.damping = juce::jlimit (0.0f, 1.0f, param (damping) / 100.0f);
                p.width = juce::jlimit (0.0f, 1.0f, param (width) / 100.0f);
                p.wetLevel = juce::jlimit (0.0f, 1.0f, param (wet) / 100.0f);
                p.dryLevel = 0.0f; // the dry part is added below, without the pre-delay
                reverb.setParameters (p);

                const auto delayFrames = rate * param (preDelay) * 0.001;
                const auto dryGain = juce::jlimit (0.0f, 1.0f, param (dry) / 100.0f);

                for (int start = 0; start < numFrames; start += chunk)
                {
                    const auto n = juce::jmin (chunk, numFrames - start);

                    for (int c = 0; c < juce::jmin (numChannels, 2); ++c)
                        for (int i = 0; i < n; ++i)
                        {
                            const auto x = data[c][start + i];
                            preDelays[(size_t) c].push (x);
                            wetBuffer[(size_t) c][(size_t) i] = delayFrames < 1.0 ? x : preDelays[(size_t) c].read (delayFrames);
                        }

                    if (numChannels >= 2)
                        reverb.processStereo (wetBuffer[0].data(), wetBuffer[1].data(), n);
                    else
                        reverb.processMono (wetBuffer[0].data(), n);

                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto& w = wetBuffer[(size_t) juce::jmin (c, 1)];
                        for (int i = 0; i < n; ++i)
                            data[c][start + i] = data[c][start + i] * dryGain + w[(size_t) i];
                    }
                }
            }

        private:
            static constexpr int chunk = 256;
            double rate = 48000.0;
            int channels = 2;
            juce::Reverb reverb;
            std::array<DelayLine, 2> preDelays;
            std::array<std::array<float, chunk>, 2> wetBuffer {};
        };
    }

    EffectType reverb()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "reverb";
        t.name = "Reverb";
        t.category = "Delay and Reverb";
        t.description = "Place the sound in a room or hall";
        t.parameters = {
            P::number ("roomSize", "Room size", 0.0f, 100.0f, 50.0f, "%", 0),
            P::number ("damping", "Damping", 0.0f, 100.0f, 50.0f, "%", 0),
            P::number ("width", "Width", 0.0f, 100.0f, 100.0f, "%", 0),
            P::number ("wet", "Reverb level", 0.0f, 100.0f, 25.0f, "%", 0),
            P::number ("dry", "Original level", 0.0f, 100.0f, 100.0f, "%", 0),
            P::number ("preDelay", "Pre-delay", 0.0f, 200.0f, 10.0f, "ms", 0),
        };
        t.factoryPresets = {
            { "Small room",   { 25, 60, 80, 15, 100, 5 } },
            { "Vocal plate",  { 50, 40, 100, 22, 100, 20 } },
            { "Concert hall", { 85, 35, 100, 30, 90, 30 } },
            { "Cathedral",    { 98, 20, 100, 40, 80, 50 } },
        };
        return withProcessor<rev::Processor> (std::move (t));
    }

    //==============================================================================
    // Chorus and flanger: a delay line whose length a slow LFO moves.
    namespace mod
    {
        enum { rate, depth, baseDelay, feedback, mix };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int numChannels) override
            {
                fs = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                for (auto& d : lines)
                    d.setMaximumFrames ((int) (fs * 0.07));
                reset();
            }

            void reset() override
            {
                for (auto& d : lines)
                    d.clear();
                phase = 0.0;
                last.fill (0.0f);
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);
                const auto base = fs * param (baseDelay) * 0.001;
                const auto swing = base * juce::jlimit (0.0f, 1.0f, param (depth) / 100.0f) * 0.95;
                const auto increment = twoPi * param (rate) / fs;
                const auto fb = juce::jlimit (-0.95f, 0.95f, param (feedback) / 100.0f);
                const auto wet = juce::jlimit (0.0f, 1.0f, param (mix) / 100.0f);

                for (int i = 0; i < numFrames; ++i)
                {
                    for (int c = 0; c < numChannels; ++c)
                    {
                        // Channels get LFOs a quarter turn apart for a wide stereo image.
                        const auto lfo = std::sin (phase + (c % 2) * twoPi * 0.25);
                        const auto d = base + swing * lfo;
                        const auto x = data[c][i];
                        lines[(size_t) c].push (x + last[(size_t) c] * fb);
                        const auto y = lines[(size_t) c].read (d);
                        last[(size_t) c] = y;
                        data[c][i] = x * (1.0f - wet * 0.5f) + y * wet;
                    }

                    phase += increment;
                    if (phase > twoPi)
                        phase -= twoPi;
                }
            }

        private:
            double fs = 48000.0, phase = 0.0;
            int channels = 2;
            std::array<DelayLine, maxChannels> lines;
            std::array<float, maxChannels> last {};
        };

        ParameterList parameters (float rateHz, float depthPercent, float delayMs, float feedbackPercent, float mixPercent)
        {
            using P = ParameterInfo;
            return {
                P::number ("rate", "Speed", 0.05f, 10.0f, rateHz, "Hz", 2, 1.0f),
                P::number ("depth", "Depth", 0.0f, 100.0f, depthPercent, "%", 0),
                P::number ("baseDelay", "Delay", 0.5f, 30.0f, delayMs, "ms", 1, 7.0f),
                P::number ("feedback", "Feedback", -95.0f, 95.0f, feedbackPercent, "%", 0),
                P::number ("mix", "Mix", 0.0f, 100.0f, mixPercent, "%", 0),
            };
        }
    }

    EffectType chorus()
    {
        EffectType t;
        t.id = "chorus";
        t.name = "Chorus";
        t.category = "Modulation";
        t.description = "Thicken the sound as if several voices played together";
        t.parameters = mod::parameters (0.8f, 40.0f, 15.0f, 0.0f, 50.0f);
        t.factoryPresets = {
            { "Subtle",  { 0.5f, 25, 12, 0, 35 } },
            { "Wide",    { 0.9f, 50, 18, 10, 50 } },
            { "Vibrato", { 5.0f, 30, 5, 0, 100 } },
        };
        return withProcessor<mod::Processor> (std::move (t));
    }

    EffectType flanger()
    {
        EffectType t;
        t.id = "flanger";
        t.name = "Flanger";
        t.category = "Modulation";
        t.description = "A sweeping, jet-like comb filter";
        t.parameters = mod::parameters (0.25f, 80.0f, 2.5f, 50.0f, 50.0f);
        t.factoryPresets = {
            { "Classic jet", { 0.2f, 90, 2.5f, 70, 50 } },
            { "Gentle",      { 0.15f, 60, 3.0f, 30, 40 } },
            { "Metallic",    { 0.5f, 70, 1.0f, -80, 50 } },
        };
        return withProcessor<mod::Processor> (std::move (t));
    }

    //==============================================================================
    namespace phs
    {
        enum { rate, depth, centre, feedback, stages, mix };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int numChannels) override
            {
                fs = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                reset();
            }

            void reset() override
            {
                for (auto& ch : state)
                    ch.fill (0.0f);
                last.fill (0.0f);
                phase = 0.0;
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);
                static constexpr int stageChoices[] { 4, 6, 8, 12 };
                const auto numStages = stageChoices[juce::jlimit (0, 3, juce::roundToInt (param (stages)))];
                const auto increment = twoPi * param (rate) / fs;
                const auto d = juce::jlimit (0.0f, 1.0f, param (depth) / 100.0f);
                const auto fc = (double) param (centre);
                const auto fb = juce::jlimit (-0.95f, 0.95f, param (feedback) / 100.0f);
                const auto wet = juce::jlimit (0.0f, 1.0f, param (mix) / 100.0f);

                for (int i = 0; i < numFrames; ++i)
                {
                    if ((i & 15) == 0)
                    {
                        // Sweep two octaves up and down around the centre frequency.
                        for (int c = 0; c < juce::jmin (numChannels, 2); ++c)
                        {
                            const auto lfo = std::sin (phase + c * twoPi * 0.25);
                            const auto f = juce::jlimit (20.0, fs * 0.45, fc * std::pow (2.0, 2.0 * d * lfo));
                            const auto t = std::tan (juce::MathConstants<double>::pi * f / fs);
                            coeff[(size_t) c] = (float) ((t - 1.0) / (t + 1.0));
                        }
                    }

                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto a = coeff[(size_t) juce::jmin (c, 1)];
                        const auto x = data[c][i];
                        auto y = x + last[(size_t) c] * fb;

                        for (int s = 0; s < numStages; ++s)
                        {
                            auto& z = state[(size_t) c][(size_t) s];
                            const auto out = a * y + z;
                            z = y - a * out;
                            y = out;
                        }

                        last[(size_t) c] = std::abs (y) < 1.0e-20f ? 0.0f : y;
                        data[c][i] = x * (1.0f - wet) + (x + y) * 0.5f * wet; // dry + all-pass mixed: the notches
                    }

                    phase += increment;
                    if (phase > twoPi)
                        phase -= twoPi;
                }
            }

        private:
            double fs = 48000.0, phase = 0.0;
            int channels = 2;
            std::array<std::array<float, 12>, maxChannels> state {};
            std::array<float, maxChannels> last {};
            std::array<float, 2> coeff {};
        };
    }

    EffectType phaser()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "phaser";
        t.name = "Phaser";
        t.category = "Modulation";
        t.description = "A soft, swirling sweep";
        t.parameters = {
            P::number ("rate", "Speed", 0.05f, 10.0f, 0.5f, "Hz", 2, 1.0f),
            P::number ("depth", "Depth", 0.0f, 100.0f, 70.0f, "%", 0),
            P::number ("centre", "Centre frequency", 100.0f, 5000.0f, 800.0f, "Hz", 0, 800.0f),
            P::number ("feedback", "Feedback", -95.0f, 95.0f, 40.0f, "%", 0),
            P::choice ("stages", "Stages", { "4", "6", "8", "12" }, 1),
            P::number ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%", 0),
        };
        t.factoryPresets = {
            { "Slow sweep",  { 0.2f, 80, 700, 50, 1, 50 } },
            { "Fast swirl",  { 3.0f, 60, 1200, 30, 0, 50 } },
            { "Deep",        { 0.4f, 100, 600, 70, 3, 60 } },
        };
        return withProcessor<phs::Processor> (std::move (t));
    }

    //==============================================================================
    namespace st
    {
        enum { width, balance, swap, invertLeft, invertRight, mono };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double, int, int) override {}
            void reset() override {}

            void process (float* const* data, int numChannels, int numFrames) override
            {
                const auto w = juce::jlimit (0.0f, 2.0f, param (width) / 100.0f);
                const auto b = juce::jlimit (-1.0f, 1.0f, param (balance) / 100.0f);
                const auto leftGain = (b > 0.0f ? 1.0f - b : 1.0f) * (param (invertLeft) > 0.5f ? -1.0f : 1.0f);
                const auto rightGain = (b < 0.0f ? 1.0f + b : 1.0f) * (param (invertRight) > 0.5f ? -1.0f : 1.0f);

                if (numChannels < 2)
                {
                    if (numChannels == 1 && leftGain != 1.0f)
                        juce::FloatVectorOperations::multiply (data[0], param (invertLeft) > 0.5f ? -1.0f : 1.0f, numFrames);
                    return;
                }

                auto* l = data[0];
                auto* r = data[1];
                const bool swapped = param (swap) > 0.5f;
                const bool toMono = param (mono) > 0.5f;

                for (int i = 0; i < numFrames; ++i)
                {
                    auto left = swapped ? r[i] : l[i];
                    auto right = swapped ? l[i] : r[i];

                    const auto mid = (left + right) * 0.5f;
                    const auto side = (left - right) * 0.5f * (toMono ? 0.0f : w);
                    left = (mid + side) * leftGain;
                    right = (mid - side) * rightGain;

                    l[i] = left;
                    r[i] = right;
                }
            }
        };
    }

    EffectType stereoTools()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "stereoTools";
        t.name = "Stereo Tools";
        t.category = "Stereo";
        t.description = "Stereo width, balance, swap and invert channels, mono";
        t.parameters = {
            P::number ("width", "Width", 0.0f, 200.0f, 100.0f, "%", 0),
            P::number ("balance", "Balance (left - right)", -100.0f, 100.0f, 0.0f, "%", 0),
            P::toggle ("swap", "Swap left and right", false),
            P::toggle ("invertLeft", "Invert left (polarity)", false),
            P::toggle ("invertRight", "Invert right (polarity)", false),
            P::toggle ("mono", "Mono (sum to both)", false),
        };
        t.factoryPresets = {
            { "Wider",       { 150, 0, 0, 0, 0, 0 } },
            { "Narrower",    { 60, 0, 0, 0, 0, 0 } },
            { "Mono",        { 100, 0, 0, 0, 0, 1 } },
            { "Swap channels", { 100, 0, 1, 0, 0, 0 } },
        };
        return withProcessor<st::Processor> (std::move (t));
    }
}
