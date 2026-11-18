// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/effects/BuiltInEffects.h"
#include "dsp/filters/Biquad.h"

#include <array>
#include <cmath>
#include <vector>

namespace mxj::dsp::builtin
{
    namespace
    {
        constexpr int maxChannels = 8;

        /** One-pole smoothing coefficient for a time constant in milliseconds. */
        float coefficient (double rate, float ms)
        {
            if (ms <= 0.0f)
                return 0.0f;

            return (float) std::exp (-1.0 / (rate * ms * 0.001));
        }

        /** Multiplies by a gain that moves linearly from `from` to `to` over the block. */
        void applyRamp (float* const* data, int numChannels, int numFrames, float from, float to)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                if (from == to)
                {
                    if (to != 1.0f)
                        juce::FloatVectorOperations::multiply (data[c], to, numFrames);
                    continue;
                }

                const auto step = (to - from) / (float) juce::jmax (1, numFrames);
                for (int i = 0; i < numFrames; ++i)
                    data[c][i] *= from + step * (float) i;
            }
        }

        template <typename Processor>
        EffectType withProcessor (EffectType t)
        {
            t.create = [] (ParameterValuesPtr v) { return std::make_unique<Processor> (std::move (v)); };
            return t;
        }
    }

    //==============================================================================
    namespace amp
    {
        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double, int, int) override { reset(); }
            void reset() override { last = dbToGain (param (0)); }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                const auto target = dbToGain (param (0));
                applyRamp (data, numChannels, numFrames, last, target);
                last = target;
            }

        private:
            float last = 1.0f;
        };
    }

    EffectType amplify()
    {
        EffectType t;
        t.id = "amplify";
        t.name = "Amplify";
        t.category = "Amplitude and Dynamics";
        t.description = "Make the audio louder or quieter by a fixed amount";
        t.parameters = { ParameterInfo::number ("gain", "Gain", -40.0f, 40.0f, 0.0f, "dB") };
        t.factoryPresets = { { "+3 dB", { 3.0f } }, { "+6 dB", { 6.0f } }, { "-3 dB", { -3.0f } }, { "-6 dB", { -6.0f } } };
        return withProcessor<amp::Processor> (std::move (t));
    }

    //==============================================================================
    // Compressor: feed-forward, stereo linked, soft knee, gain smoothed in dB.
    namespace comp
    {
        enum { threshold, ratio, attack, release, knee, makeup, detection, mix };

        /** Gain reduction in dB (>= 0) for an input level in dB. */
        inline float reductionFor (float levelDb, float thresholdDb, float ratioValue, float kneeDb) noexcept
        {
            const auto over = levelDb - thresholdDb;
            const auto slope = 1.0f - 1.0f / juce::jmax (1.0f, ratioValue);

            if (kneeDb > 0.0f && std::abs (over) * 2.0f <= kneeDb)
            {
                const auto x = over + kneeDb * 0.5f;
                return slope * x * x / (2.0f * kneeDb);
            }

            return over > 0.0f ? slope * over : 0.0f;
        }

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int maximumBlockSize, int numChannels) override
            {
                rate = sampleRate;
                dry.setSize (juce::jlimit (1, maxChannels, numChannels), juce::jmax (1, maximumBlockSize));
                reset();
            }

            void reset() override
            {
                envelopeDb = 0.0f;
                meanSquare = 0.0f;
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, dry.getNumChannels());
                numFrames = juce::jmin (numFrames, dry.getNumSamples());

                const auto thr = param (threshold);
                const auto rat = param (ratio);
                const auto kn = param (knee);
                const auto att = coefficient (rate, param (attack));
                const auto rel = coefficient (rate, param (release));
                const auto rms = param (detection) > 0.5f;
                const auto rmsCoeff = coefficient (rate, 10.0f);
                const auto makeupGain = dbToGain (param (makeup));
                const auto wet = juce::jlimit (0.0f, 1.0f, param (mix) / 100.0f);

                if (wet < 1.0f)
                    for (int c = 0; c < numChannels; ++c)
                        dry.copyFrom (c, 0, data[c], numFrames);

                float maxReduction = 0.0f;

                for (int i = 0; i < numFrames; ++i)
                {
                    float peak = 0.0f;
                    for (int c = 0; c < numChannels; ++c)
                        peak = juce::jmax (peak, std::abs (data[c][i]));

                    float levelDb;
                    if (rms)
                    {
                        meanSquare = rmsCoeff * meanSquare + (1.0f - rmsCoeff) * peak * peak;
                        levelDb = gainToDb (std::sqrt (meanSquare)) + 3.0f; // a sine's RMS reads as its peak
                    }
                    else
                    {
                        levelDb = gainToDb (peak);
                    }

                    const auto target = reductionFor (levelDb, thr, rat, kn);
                    const auto coeff = target > envelopeDb ? att : rel;
                    envelopeDb = coeff * envelopeDb + (1.0f - coeff) * target;
                    if (envelopeDb < 1.0e-6f)
                        envelopeDb = 0.0f;

                    maxReduction = juce::jmax (maxReduction, envelopeDb);
                    const auto g = dbToGain (-envelopeDb) * makeupGain;

                    for (int c = 0; c < numChannels; ++c)
                        data[c][i] *= g;
                }

                if (wet < 1.0f)
                    for (int c = 0; c < numChannels; ++c)
                    {
                        juce::FloatVectorOperations::multiply (data[c], wet, numFrames);
                        juce::FloatVectorOperations::addWithMultiply (data[c], dry.getReadPointer (c), 1.0f - wet, numFrames);
                    }

                params->gainReductionDb.store (maxReduction, std::memory_order_relaxed);
            }

        private:
            double rate = 48000.0;
            float envelopeDb = 0.0f, meanSquare = 0.0f;
            juce::AudioBuffer<float> dry;
        };
    }

    EffectType compressor()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "compressor";
        t.name = "Compressor";
        t.category = "Amplitude and Dynamics";
        t.description = "Even out loud and quiet parts";
        t.showsGainReduction = true;
        t.parameters = {
            P::number ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB"),
            P::number ("ratio", "Ratio", 1.0f, 20.0f, 3.0f, ":1", 1, 4.0f),
            P::number ("attack", "Attack", 0.1f, 200.0f, 10.0f, "ms", 1, 15.0f),
            P::number ("release", "Release", 10.0f, 2000.0f, 150.0f, "ms", 0, 200.0f),
            P::number ("knee", "Knee", 0.0f, 24.0f, 6.0f, "dB"),
            P::number ("makeup", "Make-up gain", -12.0f, 24.0f, 0.0f, "dB"),
            P::choice ("detection", "Detection", { "Peak", "RMS" }, 1),
            P::number ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0),
        };
        //                                  thr   ratio att  rel  knee makeup det mix
        t.factoryPresets = {
            { "Podcast voice",            { -20, 3.0f, 8,  150, 6, 4, 1, 100 } },
            { "Gentle levelling",         { -24, 2.0f, 20, 250, 10, 3, 1, 100 } },
            { "Strong voice-over",        { -26, 6.0f, 3,  100, 6, 8, 1, 100 } },
            { "Music bus glue",           { -14, 2.0f, 30, 300, 6, 2, 1, 100 } },
            { "Parallel (New York)",      { -30, 8.0f, 2,  120, 3, 10, 0, 40 } },
        };
        return withProcessor<comp::Processor> (std::move (t));
    }

    //==============================================================================
    // Limiter: look-ahead brick wall. A sliding minimum of the needed gain over the
    // look-ahead window, smoothed by a moving average of the same length, can
    // never let a peak through (see the comment in process()).
    namespace lim
    {
        enum { inputGain, ceiling, release };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            static constexpr double lookAheadMs = 5.0;

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                window = juce::jmax (2, (int) std::lround (rate * lookAheadMs * 0.001));
                lookAheadLine.setSize (channels, window);
                held.assign ((size_t) window, 1.0f);
                dequeValues.assign ((size_t) window, 1.0f);
                dequeIndices.assign ((size_t) window, 0);
                reset();
            }

            void reset() override
            {
                lookAheadLine.clear();
                std::fill (held.begin(), held.end(), 1.0f);
                writePos = 0;
                frameCounter = 0;
                dequeHead = dequeSize = 0;
                heldSum = (double) window;
                envelope = 1.0f;
            }

            int getLatencyFrames() const override { return window - 1; }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, channels);
                const auto inGain = dbToGain (param (inputGain));
                const auto ceil = dbToGain (param (ceiling));
                const auto rel = coefficient (rate, param (release));
                float maxReduction = 0.0f;

                for (int i = 0; i < numFrames; ++i)
                {
                    float peak = 0.0f;
                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto x = data[c][i] * inGain;
                        peak = juce::jmax (peak, std::abs (x));
                        lookAheadLine.setSample (c, writePos, x);
                    }

                    // Gain this frame needs, then the minimum over the last `window` frames.
                    const auto needed = peak > ceil ? ceil / peak : 1.0f;
                    pushMinimum (needed);
                    const auto minimum = dequeValues[(size_t) dequeHead];

                    // Moving average of the held minimum: for a peak that leaves the delay
                    // line now, every value in the average was at most its needed gain.
                    heldSum += (double) minimum - (double) held[(size_t) writePos];
                    held[(size_t) writePos] = minimum;
                    auto smooth = (float) (heldSum / (double) window);

                    // Release slowly, never attack slower than the average.
                    envelope = smooth < envelope ? smooth : smooth + (envelope - smooth) * rel;
                    const auto g = juce::jmin (envelope, smooth);
                    maxReduction = juce::jmax (maxReduction, -gainToDb (g));

                    const auto readPos = (writePos + 1) % window; // the oldest frame: window - 1 frames ago
                    for (int c = 0; c < numChannels; ++c)
                        data[c][i] = juce::jlimit (-ceil, ceil, lookAheadLine.getSample (c, readPos) * g);

                    writePos = readPos;
                    ++frameCounter;
                }

                params->gainReductionDb.store (maxReduction, std::memory_order_relaxed);
            }

        private:
            /** Sliding window minimum (monotonic deque in a fixed ring, no allocation). */
            void pushMinimum (float value)
            {
                const auto n = (int) dequeValues.size();

                while (dequeSize > 0)
                {
                    const auto back = (dequeHead + dequeSize - 1) % n;
                    if (dequeValues[(size_t) back] < value)
                        break;
                    --dequeSize;
                }

                const auto slot = (dequeHead + dequeSize) % n;
                dequeValues[(size_t) slot] = value;
                dequeIndices[(size_t) slot] = frameCounter;
                ++dequeSize;

                if (dequeIndices[(size_t) dequeHead] <= frameCounter - window)
                {
                    dequeHead = (dequeHead + 1) % n;
                    --dequeSize;
                }
            }

            double rate = 48000.0;
            int channels = 2, window = 240, writePos = 0;
            juce::int64 frameCounter = 0;
            juce::AudioBuffer<float> lookAheadLine;
            std::vector<float> held, dequeValues;
            std::vector<juce::int64> dequeIndices;
            int dequeHead = 0, dequeSize = 0;
            double heldSum = 0.0;
            float envelope = 1.0f;
        };
    }

    EffectType limiter()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "limiter";
        t.name = "Limiter";
        t.category = "Amplitude and Dynamics";
        t.description = "Make the audio louder without letting peaks pass a ceiling";
        t.showsGainReduction = true;
        t.parameters = {
            P::number ("inputGain", "Input gain", 0.0f, 24.0f, 0.0f, "dB"),
            P::number ("ceiling", "Ceiling", -24.0f, 0.0f, -1.0f, "dB"),
            P::number ("release", "Release", 1.0f, 1000.0f, 60.0f, "ms", 0, 80.0f),
        };
        t.factoryPresets = {
            { "Safety (-1 dB)",   { 0, -1, 60 } },
            { "Podcast loud",     { 6, -1, 80 } },
            { "Broadcast (-2 dB)", { 3, -2, 100 } },
        };
        return withProcessor<lim::Processor> (std::move (t));
    }

    //==============================================================================
    // Noise gate: quietens the audio while it stays below the threshold.
    namespace gt
    {
        enum { threshold, reduction, attack, hold, release };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int) override { rate = sampleRate; reset(); }

            void reset() override
            {
                level = 0.0f;
                gainDb = 0.0f;
                holdLeft = 0;
                open = true;
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, maxChannels);
                const auto openAt = dbToGain (param (threshold));
                const auto closeAt = openAt * dbToGain (-3.0f); // a little hysteresis stops chattering
                const auto floorDb = -param (reduction);
                const auto att = coefficient (rate, param (attack));
                const auto rel = coefficient (rate, param (release));
                const auto holdFrames = (int) (rate * param (hold) * 0.001);
                const auto levelRelease = coefficient (rate, 20.0f);
                float maxReduction = 0.0f;

                for (int i = 0; i < numFrames; ++i)
                {
                    float peak = 0.0f;
                    for (int c = 0; c < numChannels; ++c)
                        peak = juce::jmax (peak, std::abs (data[c][i]));

                    level = peak > level ? peak : levelRelease * level + (1.0f - levelRelease) * peak;

                    if (level >= openAt)
                    {
                        open = true;
                        holdLeft = holdFrames;
                    }
                    else if (level < closeAt)
                    {
                        if (holdLeft > 0)
                            --holdLeft;
                        else
                            open = false;
                    }

                    // Smoothed in dB, so the gain falls evenly to the floor and rises evenly back.
                    const auto target = open ? 0.0f : floorDb;
                    const auto coeff = target > gainDb ? att : rel;
                    gainDb = coeff * gainDb + (1.0f - coeff) * target;
                    const auto gain = dbToGain (gainDb);

                    for (int c = 0; c < numChannels; ++c)
                        data[c][i] *= gain;

                    maxReduction = juce::jmax (maxReduction, -gainDb);
                }

                params->gainReductionDb.store (maxReduction, std::memory_order_relaxed);
            }

        private:
            double rate = 48000.0;
            float level = 0.0f, gainDb = 0.0f;
            int holdLeft = 0;
            bool open = true;
        };
    }

    EffectType gate()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "gate";
        t.name = "Noise Gate";
        t.category = "Amplitude and Dynamics";
        t.description = "Quieten background noise in the pauses";
        t.showsGainReduction = true;
        t.parameters = {
            P::number ("threshold", "Threshold", -80.0f, 0.0f, -45.0f, "dB"),
            P::number ("reduction", "Reduction", 0.0f, 80.0f, 20.0f, "dB"),
            P::number ("attack", "Attack", 0.1f, 50.0f, 2.0f, "ms", 1, 5.0f),
            P::number ("hold", "Hold", 0.0f, 500.0f, 80.0f, "ms", 0, 100.0f),
            P::number ("release", "Release", 5.0f, 2000.0f, 150.0f, "ms", 0, 200.0f),
        };
        t.factoryPresets = {
            { "Voice, gentle",     { -50, 12, 2, 120, 200 } },
            { "Voice, strong",     { -42, 30, 1, 80, 120 } },
            { "Remove room noise", { -55, 18, 3, 150, 300 } },
        };
        return withProcessor<gt::Processor> (std::move (t));
    }

    //==============================================================================
    // De-esser: splits the audio at `frequency` with a Linkwitz-Riley crossover
    // (low + high add up to the input with a flat response) and turns only the
    // high band down while it is too loud.
    namespace des
    {
        enum { frequency, threshold, maxReduction };

        class Processor final : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int) override
            {
                rate = sampleRate;
                lastFrequency = -1.0f;
                reset();
            }

            void reset() override
            {
                for (auto& ch : bands)
                    for (auto& f : ch)
                        f.reset();
                level = 0.0f;
                reductionDb = 0.0f;
            }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                numChannels = juce::jmin (numChannels, maxChannels);

                if (param (frequency) != lastFrequency)
                {
                    lastFrequency = param (frequency);
                    const auto lp = BiquadCoefficients::lowPass (rate, lastFrequency, 0.7071);
                    const auto hp = BiquadCoefficients::highPass (rate, lastFrequency, 0.7071);
                    for (auto& ch : bands)
                    {
                        ch[0].setCoefficients (lp);
                        ch[1].setCoefficients (lp);
                        ch[2].setCoefficients (hp);
                        ch[3].setCoefficients (hp);
                    }
                }

                const auto thr = param (threshold);
                const auto maxRed = param (maxReduction);
                const auto att = coefficient (rate, 1.0f);
                const auto rel = coefficient (rate, 60.0f);
                float shownReduction = 0.0f;

                for (int i = 0; i < numFrames; ++i)
                {
                    std::array<float, maxChannels> low {}, high {};
                    float peak = 0.0f;

                    for (int c = 0; c < numChannels; ++c)
                    {
                        auto& f = bands[(size_t) c];
                        const auto x = data[c][i];
                        low[(size_t) c] = f[1].processSample (f[0].processSample (x));
                        high[(size_t) c] = f[3].processSample (f[2].processSample (x));
                        peak = juce::jmax (peak, std::abs (high[(size_t) c]));
                    }

                    level = peak > level ? att * level + (1.0f - att) * peak : rel * level + (1.0f - rel) * peak;
                    const auto over = gainToDb (level) - thr;
                    const auto target = over > 0.0f ? juce::jmin (maxRed, over * 0.75f) : 0.0f;
                    reductionDb = target > reductionDb ? target : rel * reductionDb + (1.0f - rel) * target;

                    const auto highGain = dbToGain (-reductionDb);
                    for (int c = 0; c < numChannels; ++c)
                        data[c][i] = low[(size_t) c] + high[(size_t) c] * highGain;

                    shownReduction = juce::jmax (shownReduction, reductionDb);
                }

                for (auto& ch : bands)
                    for (auto& f : ch)
                        f.sanitise();

                params->gainReductionDb.store (shownReduction, std::memory_order_relaxed);
            }

        private:
            double rate = 48000.0;
            float lastFrequency = -1.0f, level = 0.0f, reductionDb = 0.0f;
            std::array<std::array<Biquad, 4>, maxChannels> bands {}; ///< low-pass x2, high-pass x2
        };
    }

    EffectType deEsser()
    {
        using P = ParameterInfo;
        EffectType t;
        t.id = "deEsser";
        t.name = "De-esser";
        t.category = "Amplitude and Dynamics";
        t.description = "Soften sharp 's' and 'sh' sounds in speech";
        t.showsGainReduction = true;
        t.parameters = {
            P::number ("frequency", "Frequency", 2000.0f, 12000.0f, 5500.0f, "Hz", 0, 5500.0f),
            P::number ("threshold", "Threshold", -60.0f, 0.0f, -28.0f, "dB"),
            P::number ("maxReduction", "Maximum reduction", 0.0f, 24.0f, 8.0f, "dB"),
        };
        t.factoryPresets = {
            { "Male voice",   { 4500, -28, 8 } },
            { "Female voice", { 6500, -28, 8 } },
            { "Strong",       { 5500, -34, 14 } },
        };
        return withProcessor<des::Processor> (std::move (t));
    }
}
