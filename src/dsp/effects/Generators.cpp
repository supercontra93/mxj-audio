// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/BuiltInEffects.h"

#include <array>
#include <cmath>

namespace mxj::dsp::builtin
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;

        ParameterInfo durationParameter()
        {
            return ParameterInfo::number ("duration", "Duration", 0.1f, 600.0f, 5.0f, "s", 1, 10.0f);
        }
    }

    namespace tone
    {
        enum { waveform, frequency, level, duration };

        /** Writes a tone, ignoring the input. */
        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int) override { rate = sampleRate; reset(); }
            void reset() override { phase = 0.0; }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                const auto shape = juce::roundToInt (param (waveform));
                const auto amplitude = dbToGain (param (level));
                const auto increment = param (frequency) / rate;

                for (int i = 0; i < numFrames; ++i)
                {
                    float v;
                    switch (shape)
                    {
                        case 1:  v = phase < 0.5 ? 1.0f : -1.0f; break;                              // square
                        case 2:  v = (float) (2.0 * phase - 1.0); break;                             // sawtooth
                        case 3:  v = (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); break; // triangle
                        default: v = (float) std::sin (twoPi * phase); break;                        // sine
                    }

                    for (int c = 0; c < numChannels; ++c)
                        data[c][i] = v * amplitude;

                    phase += increment;
                    phase -= std::floor (phase);
                }
            }

        private:
            double rate = 48000.0, phase = 0.0;
        };
    }

    EffectType toneGenerator()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "tone";
        t.name = "Tone";
        t.category = "Generate";
        t.description = "Insert a test tone at the cursor";
        t.kind = EffectType::Kind::generator;
        t.parameters = {
            P::choice ("waveform", "Waveform", { "Sine", "Square", "Sawtooth", "Triangle" }, 0),
            P::number ("frequency", "Frequency", 20.0f, 20000.0f, 440.0f, "Hz", 1, 1000.0f),
            P::number ("level", "Level", -60.0f, 0.0f, -6.0f, "dB"),
            durationParameter(),
        };
        t.factoryPresets = {
            { "1 kHz reference (-20 dB)", { 0, 1000, -20, 5 } },
            { "Concert A (440 Hz)",       { 0, 440, -6, 5 } },
            { "Censor beep",              { 0, 1000, -10, 1 } },
        };
        t.create = [] (ParameterValuesPtr v) { return std::make_unique<tone::Processor> (std::move (v)); };
        return t;
    }

    //==============================================================================
    namespace noise
    {
        enum { colour, level, duration };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double, int, int numChannels) override
            {
                channels = juce::jlimit (1, 8, numChannels);
                reset();
            }

            void reset() override
            {
                random.setSeed (0x4d584a);
                for (auto& s : pink)
                    s.fill (0.0f);
                brown.fill (0.0f);
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);
                const auto kind = juce::roundToInt (param (colour));
                const auto amplitude = dbToGain (param (level));

                for (int c = 0; c < numChannels; ++c)
                    for (int i = 0; i < numFrames; ++i)
                    {
                        const auto white = random.nextFloat() * 2.0f - 1.0f;
                        float v = white;

                        if (kind == 1)
                        {
                            // Paul Kellet's "economy" pink noise filter (public domain).
                            auto& b = pink[(size_t) c];
                            b[0] = 0.99765f * b[0] + white * 0.0990460f;
                            b[1] = 0.96300f * b[1] + white * 0.2965164f;
                            b[2] = 0.57000f * b[2] + white * 1.0526913f;
                            v = (b[0] + b[1] + b[2] + white * 0.1848f) * 0.25f;
                        }
                        else if (kind == 2)
                        {
                            auto& b = brown[(size_t) c];
                            b = juce::jlimit (-1.0f, 1.0f, (b + 0.02f * white) * 0.998f);
                            v = b * 3.0f;
                        }

                        data[c][i] = juce::jlimit (-1.0f, 1.0f, v) * amplitude;
                    }
            }

        private:
            int channels = 2;
            juce::Random random;
            std::array<std::array<float, 3>, 8> pink {};
            std::array<float, 8> brown {};
        };
    }

    EffectType noiseGenerator()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "noise";
        t.name = "Noise";
        t.category = "Generate";
        t.description = "Insert white, pink or brown noise at the cursor";
        t.kind = EffectType::Kind::generator;
        t.parameters = {
            P::choice ("colour", "Colour", { "White", "Pink", "Brown" }, 1),
            P::number ("level", "Level", -60.0f, 0.0f, -18.0f, "dB"),
            durationParameter(),
        };
        t.create = [] (ParameterValuesPtr v) { return std::make_unique<noise::Processor> (std::move (v)); };
        return t;
    }
}
