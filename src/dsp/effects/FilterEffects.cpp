// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/BuiltInEffects.h"
#include "dsp/filters/Biquad.h"

#include <array>

namespace mxj::dsp::builtin
{
    namespace
    {
        /** A chain of biquads plus an output gain: what every equaliser and filter here is. */
        struct Cascade
        {
            static constexpr int maxStages = 16;
            std::array<BiquadCoefficients, maxStages> stages {};
            int count = 0;
            float outputGainDb = 0.0f;

            void add (const BiquadCoefficients& c) noexcept
            {
                if (count < maxStages)
                    stages[(size_t) count++] = c;
            }

            double magnitude (double rate, double hz) const
            {
                double m = 1.0;
                for (int i = 0; i < count; ++i)
                    m *= stages[(size_t) i].magnitude (rate, hz);

                return m * std::pow (10.0, outputGainDb / 20.0);
            }
        };

        using Design = Cascade (*) (const float* values, double sampleRate);

        /** Runs a Cascade designed from the parameters; redesigns only when a value changes. */
        class CascadeProcessor final : public EffectProcessor
        {
        public:
            CascadeProcessor (ParameterValuesPtr v, Design d) : EffectProcessor (std::move (v)), design (d) {}

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                cached.fill (std::numeric_limits<float>::quiet_NaN()); // forces a redesign
                reset();
            }

            void reset() override
            {
                for (auto& ch : filters)
                    for (auto& f : ch)
                        f.reset();
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                updateDesign();
                numChannels = juce::jmin (numChannels, channels);

                for (int c = 0; c < numChannels; ++c)
                {
                    for (int s = 0; s < current.count; ++s)
                    {
                        auto& f = filters[(size_t) c][(size_t) s];
                        f.process (data[c], numFrames);
                        f.sanitise();
                    }

                    applyGain (data[c], numFrames);
                }

                lastGain = targetGain;
            }

        private:
            static constexpr int maxChannels = 8;
            static constexpr int maxValues = 32;

            void updateDesign()
            {
                const auto n = juce::jmin (maxValues, params->size());
                bool changed = false;
                std::array<float, maxValues> now {};

                for (int i = 0; i < n; ++i)
                {
                    now[(size_t) i] = param (i);
                    if (! (now[(size_t) i] == cached[(size_t) i]))
                        changed = true;
                }

                if (! changed)
                    return;

                cached = now;
                const auto previousCount = current.count;
                current = design (now.data(), rate);
                targetGain = dbToGain (current.outputGainDb);

                if (current.count != previousCount)
                    reset();

                for (auto& ch : filters)
                    for (int s = 0; s < current.count; ++s)
                        ch[(size_t) s].setCoefficients (current.stages[(size_t) s]);
            }

            void applyGain (float* d, int numFrames) const noexcept
            {
                if (lastGain == targetGain)
                {
                    if (targetGain != 1.0f)
                        juce::FloatVectorOperations::multiply (d, targetGain, numFrames);
                    return;
                }

                const auto step = (targetGain - lastGain) / (float) juce::jmax (1, numFrames);
                for (int i = 0; i < numFrames; ++i)
                    d[i] *= lastGain + step * (float) i;
            }

            Design design;
            double rate = 48000.0;
            int channels = 2;
            Cascade current;
            std::array<float, maxValues> cached {};
            std::array<std::array<Biquad, Cascade::maxStages>, maxChannels> filters {};
            float lastGain = 1.0f, targetGain = 1.0f;
        };

        EffectType makeCascadeType (EffectType t, Design d)
        {
            t.create = [d] (ParameterValuesPtr v) { return std::make_unique<CascadeProcessor> (std::move (v), d); };
            t.response = [d] (const std::vector<float>& values, double rate, double hz)
            {
                std::array<float, 32> v {};
                for (size_t i = 0; i < juce::jmin (values.size(), v.size()); ++i)
                    v[i] = values[i];

                return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, d (v.data(), rate).magnitude (rate, hz))));
            };
            return t;
        }

        /** Butterworth sections for 12, 24 or 48 dB per octave. */
        void addButterworth (Cascade& c, bool highPass, double rate, double hz, int slopeIndex)
        {
            static constexpr double q2[] { 0.70710678 };
            static constexpr double q4[] { 0.54119610, 1.30656296 };
            static constexpr double q8[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };

            const double* qs = slopeIndex <= 0 ? q2 : slopeIndex == 1 ? q4 : q8;
            const int n = slopeIndex <= 0 ? 1 : slopeIndex == 1 ? 2 : 4;

            for (int i = 0; i < n; ++i)
                c.add (highPass ? BiquadCoefficients::highPass (rate, hz, qs[i])
                                : BiquadCoefficients::lowPass (rate, hz, qs[i]));
        }
    }

    //==============================================================================
    // Parametric EQ: low cut, low shelf, three bells, high shelf, high cut.
    namespace peq
    {
        enum { hpOn, hpFreq, lowFreq, lowGain, b1Freq, b1Gain, b1Q, b2Freq, b2Gain, b2Q,
               b3Freq, b3Gain, b3Q, highFreq, highGain, lpOn, lpFreq, output };

        Cascade design (const float* v, double rate)
        {
            Cascade c;
            if (v[hpOn] > 0.5f)
                addButterworth (c, true, rate, v[hpFreq], 1);
            if (v[lowGain] != 0.0f)
                c.add (BiquadCoefficients::lowShelf (rate, v[lowFreq], 0.707, v[lowGain]));
            if (v[b1Gain] != 0.0f)
                c.add (BiquadCoefficients::peak (rate, v[b1Freq], v[b1Q], v[b1Gain]));
            if (v[b2Gain] != 0.0f)
                c.add (BiquadCoefficients::peak (rate, v[b2Freq], v[b2Q], v[b2Gain]));
            if (v[b3Gain] != 0.0f)
                c.add (BiquadCoefficients::peak (rate, v[b3Freq], v[b3Q], v[b3Gain]));
            if (v[highGain] != 0.0f)
                c.add (BiquadCoefficients::highShelf (rate, v[highFreq], 0.707, v[highGain]));
            if (v[lpOn] > 0.5f)
                addButterworth (c, false, rate, v[lpFreq], 1);
            c.outputGainDb = v[output];
            return c;
        }
    }

    EffectType parametricEq()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "parametricEq";
        t.name = "Parametric EQ";
        t.category = "Filter and EQ";
        t.description = "Shape the tone: cut rumble, warm up or brighten a voice";
        t.parameters = {
            P::toggle ("hpOn",   "Low cut", false),
            P::number ("hpFreq", "Low cut frequency", 20.0f, 1000.0f, 80.0f, "Hz", 0, 150.0f),
            P::number ("lowFreq", "Low shelf frequency", 20.0f, 1000.0f, 120.0f, "Hz", 0, 150.0f),
            P::number ("lowGain", "Low shelf gain", -18.0f, 18.0f, 0.0f, "dB"),
            P::number ("b1Freq", "Band 1 frequency", 20.0f, 20000.0f, 250.0f, "Hz", 0, 1000.0f),
            P::number ("b1Gain", "Band 1 gain", -18.0f, 18.0f, 0.0f, "dB"),
            P::number ("b1Q",    "Band 1 width (Q)", 0.1f, 10.0f, 1.0f, "", 2, 1.0f),
            P::number ("b2Freq", "Band 2 frequency", 20.0f, 20000.0f, 1000.0f, "Hz", 0, 1000.0f),
            P::number ("b2Gain", "Band 2 gain", -18.0f, 18.0f, 0.0f, "dB"),
            P::number ("b2Q",    "Band 2 width (Q)", 0.1f, 10.0f, 1.0f, "", 2, 1.0f),
            P::number ("b3Freq", "Band 3 frequency", 20.0f, 20000.0f, 4000.0f, "Hz", 0, 1000.0f),
            P::number ("b3Gain", "Band 3 gain", -18.0f, 18.0f, 0.0f, "dB"),
            P::number ("b3Q",    "Band 3 width (Q)", 0.1f, 10.0f, 1.0f, "", 2, 1.0f),
            P::number ("highFreq", "High shelf frequency", 1000.0f, 20000.0f, 8000.0f, "Hz", 0, 5000.0f),
            P::number ("highGain", "High shelf gain", -18.0f, 18.0f, 0.0f, "dB"),
            P::toggle ("lpOn",   "High cut", false),
            P::number ("lpFreq", "High cut frequency", 1000.0f, 20000.0f, 16000.0f, "Hz", 0, 5000.0f),
            P::number ("output", "Output gain", -18.0f, 18.0f, 0.0f, "dB"),
        };
        //                                   hpOn hpF  loF  loG  b1F  b1G  b1Q  b2F   b2G  b2Q  b3F   b3G  b3Q  hiF   hiG  lpOn lpF    out
        t.factoryPresets = {
            { "Podcast voice",     { 1, 80,  120, 0,   250, -2, 1.0f, 3000, 2,   1.0f, 6500, -1, 2.0f, 10000, 2, 0, 16000, 0 } },
            { "Remove rumble",     { 1, 90,  120, 0,   250, 0,  1.0f, 1000, 0,   1.0f, 4000, 0,  1.0f, 8000, 0,  0, 16000, 0 } },
            { "Warm voice",        { 1, 70,  150, 2,   350, -1, 1.2f, 2500, 0,   1.0f, 4000, 0,  1.0f, 9000, -1, 0, 16000, 0 } },
            { "Bright and clear",  { 1, 80,  120, 0,   300, -2, 1.0f, 2500, 1,   1.0f, 5000, 2,  1.0f, 12000, 3, 0, 16000, 0 } },
            { "Telephone",         { 1, 400, 120, 0,   250, 0,  1.0f, 1500, 4,   0.8f, 4000, 0,  1.0f, 8000, 0,  1, 3400,  0 } },
        };
        return makeCascadeType (std::move (t), peq::design);
    }

    //==============================================================================
    namespace geq
    {
        constexpr double bands[] { 31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };

        Cascade design (const float* v, double rate)
        {
            Cascade c;
            for (int i = 0; i < 10; ++i)
                if (v[i] != 0.0f && bands[i] < rate * 0.45)
                    c.add (BiquadCoefficients::peak (rate, bands[i], 1.41, v[i]));

            c.outputGainDb = v[10];
            return c;
        }
    }

    EffectType graphicEq()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "graphicEq";
        t.name = "Graphic EQ (10 bands)";
        t.category = "Filter and EQ";
        t.description = "Raise or lower ten fixed frequency bands";
        t.parameters = {
            P::number ("g31",  "31 Hz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g63",  "63 Hz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g125", "125 Hz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g250", "250 Hz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g500", "500 Hz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g1k",  "1 kHz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g2k",  "2 kHz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g4k",  "4 kHz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g8k",  "8 kHz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("g16k", "16 kHz", -12.0f, 12.0f, 0.0f, "dB"),
            P::number ("output", "Output gain", -18.0f, 18.0f, 0.0f, "dB"),
        };
        t.factoryPresets = {
            { "More bass",   { 4, 4, 3, 1, 0, 0, 0, 0, 0, 0, -2 } },
            { "More treble", { 0, 0, 0, 0, 0, 0, 1, 2, 4, 4, -2 } },
            { "Loudness",    { 4, 3, 1, 0, -1, -1, 0, 1, 3, 3, -3 } },
            { "Less mud",    { 0, 0, -1, -3, -2, 0, 0, 0, 0, 0, 0 } },
        };
        return makeCascadeType (std::move (t), geq::design);
    }

    //==============================================================================
    namespace flt
    {
        enum { type, freq, slope, q, output };

        Cascade design (const float* v, double rate)
        {
            Cascade c;
            const auto t = juce::roundToInt (v[type]);
            const auto s = juce::roundToInt (v[slope]);

            if (t == 0)
                addButterworth (c, true, rate, v[freq], s);
            else if (t == 1)
                addButterworth (c, false, rate, v[freq], s);
            else if (t == 2)
                for (int i = 0; i <= s; ++i)
                    c.add (BiquadCoefficients::bandPass (rate, v[freq], v[q]));
            else
            {
                // Notch: a deep, narrow cut.
                for (int i = 0; i <= s; ++i)
                    c.add (BiquadCoefficients::peak (rate, v[freq], v[q], -60.0));
            }

            c.outputGainDb = v[output];
            return c;
        }
    }

    EffectType filter()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "filter";
        t.name = "Filter";
        t.category = "Filter and EQ";
        t.description = "High-pass, low-pass, band-pass or notch filter";
        t.parameters = {
            P::choice ("type", "Type", { "High-pass (low cut)", "Low-pass (high cut)", "Band-pass", "Notch" }, 0),
            P::number ("freq", "Frequency", 20.0f, 20000.0f, 100.0f, "Hz", 0, 1000.0f),
            P::choice ("slope", "Slope", { "12 dB/octave", "24 dB/octave", "48 dB/octave" }, 1),
            P::number ("q", "Width (Q)", 0.1f, 20.0f, 0.7f, "", 2, 1.0f),
            P::number ("output", "Output gain", -18.0f, 18.0f, 0.0f, "dB"),
        };
        t.factoryPresets = {
            { "Low cut 80 Hz",    { 0, 80, 1, 0.7f, 0 } },
            { "High cut 8 kHz",   { 1, 8000, 1, 0.7f, 0 } },
            { "Hum notch 50 Hz",  { 3, 50, 0, 10.0f, 0 } },
            { "Hum notch 60 Hz",  { 3, 60, 0, 10.0f, 0 } },
            { "Voice band",       { 2, 1200, 0, 0.5f, 0 } },
        };
        return makeCascadeType (std::move (t), flt::design);
    }
}
