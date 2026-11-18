// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/repair/Restoration.h"
#include "dsp/effects/BuiltInEffects.h"
#include "dsp/filters/Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mxj::dsp
{
    namespace restore
    {
        std::vector<double> predictionCoefficients (const float* x, int length, int order, const std::vector<bool>* skip)
        {
            std::vector<double> r ((size_t) order + 1, 0.0);
            for (int lag = 0; lag <= order; ++lag)
            {
                double sum = 0.0;
                for (int i = lag; i < length; ++i)
                {
                    if (skip != nullptr && ((*skip)[(size_t) i] || (*skip)[(size_t) (i - lag)]))
                        continue;
                    sum += (double) x[i] * (double) x[i - lag];
                }
                r[(size_t) lag] = sum;
            }

            std::vector<double> a ((size_t) order + 1, 0.0);
            a[0] = 1.0;
            if (r[0] <= 1.0e-12)
                return a;

            r[0] *= 1.0 + 1.0e-9; // a little white noise keeps the model stable
            double error = r[0];
            std::vector<double> previous ((size_t) order + 1);

            for (int i = 1; i <= order; ++i)
            {
                double acc = r[(size_t) i];
                for (int j = 1; j < i; ++j)
                    acc += a[(size_t) j] * r[(size_t) (i - j)];

                const double k = -acc / error;
                previous = a;
                for (int j = 1; j < i; ++j)
                    a[(size_t) j] = previous[(size_t) j] + k * previous[(size_t) (i - j)];
                a[(size_t) i] = k;

                error *= 1.0 - k * k;
                if (error <= 1.0e-15)
                    break;
            }

            return a;
        }

        bool interpolate (float* x, int length, int gapStart, int gapLength, int order)
        {
            if (gapLength <= 0)
                return true;

            if (gapStart < order || gapStart + gapLength + order > length || order < 1)
                return false;

            // Model from the samples around the gap.
            std::vector<bool> skip ((size_t) length, false);
            for (int i = gapStart; i < gapStart + gapLength; ++i)
                skip[(size_t) i] = true;
            const auto a = predictionCoefficients (x, length, order, &skip);

            // Least squares: minimise the prediction error of every row that touches the gap.
            const int g = gapLength;
            std::vector<double> m ((size_t) (g * g), 0.0), v ((size_t) g, 0.0), row ((size_t) g);

            for (int t = gapStart; t < gapStart + g + order; ++t)
            {
                double known = 0.0;
                std::fill (row.begin(), row.end(), 0.0);

                for (int i = 0; i <= order; ++i)
                {
                    const int idx = t - i;
                    if (idx >= gapStart && idx < gapStart + g)
                        row[(size_t) (idx - gapStart)] += a[(size_t) i];
                    else
                        known += a[(size_t) i] * x[idx];
                }

                for (int p = 0; p < g; ++p)
                {
                    if (row[(size_t) p] == 0.0)
                        continue;
                    v[(size_t) p] -= row[(size_t) p] * known;
                    for (int q = 0; q < g; ++q)
                        m[(size_t) (p * g + q)] += row[(size_t) p] * row[(size_t) q];
                }
            }

            // Cholesky decomposition (the matrix is symmetric positive definite).
            for (int p = 0; p < g; ++p)
                m[(size_t) (p * g + p)] += 1.0e-9;

            for (int j = 0; j < g; ++j)
            {
                double d = m[(size_t) (j * g + j)];
                for (int k = 0; k < j; ++k)
                    d -= m[(size_t) (j * g + k)] * m[(size_t) (j * g + k)];
                if (d <= 0.0)
                    return false;
                d = std::sqrt (d);
                m[(size_t) (j * g + j)] = d;

                for (int i = j + 1; i < g; ++i)
                {
                    double s = m[(size_t) (i * g + j)];
                    for (int k = 0; k < j; ++k)
                        s -= m[(size_t) (i * g + k)] * m[(size_t) (j * g + k)];
                    m[(size_t) (i * g + j)] = s / d;
                }
            }

            std::vector<double> y ((size_t) g);
            for (int i = 0; i < g; ++i)
            {
                double s = v[(size_t) i];
                for (int k = 0; k < i; ++k)
                    s -= m[(size_t) (i * g + k)] * y[(size_t) k];
                y[(size_t) i] = s / m[(size_t) (i * g + i)];
            }

            for (int i = g; --i >= 0;)
            {
                double s = y[(size_t) i];
                for (int k = i + 1; k < g; ++k)
                    s -= m[(size_t) (k * g + i)] * y[(size_t) k];
                y[(size_t) i] = s / m[(size_t) (i * g + i)];
            }

            for (int i = 0; i < g; ++i)
                if (! std::isfinite (y[(size_t) i]))
                    return false;

            for (int i = 0; i < g; ++i)
                x[gapStart + i] = (float) y[(size_t) i];

            return true;
        }

        namespace
        {
            constexpr int maxChannels = 64;

            /** Runs `repairBlock` over the input in blocks with `margin` samples of
                context on each side; only the middle of each block is kept. */
            template <typename RepairBlock>
            AudioSequence processBlocks (const AudioSequence& input, int blockSize, int margin, BlockStore& store,
                                         const WorkControl& control, RepairBlock&& repairBlock)
            {
                const auto channels = juce::jmin (input.getNumChannels(), maxChannels);
                const auto length = input.getNumFrames();
                if (channels <= 0)
                    return {};

                SequenceWriter out (store, channels, input.getSampleRate(), length);
                juce::AudioBuffer<float> buffer (channels, blockSize + 2 * margin);

                for (juce::int64 start = 0; start < length; start += blockSize)
                {
                    if (control.cancelled())
                    {
                        out.finish();
                        return {};
                    }

                    const auto n = (int) juce::jmin ((juce::int64) blockSize, length - start);
                    buffer.clear();
                    const auto from = juce::jmax ((juce::int64) 0, start - margin);
                    const auto to = juce::jmin (length, start + n + margin);
                    float* pointers[maxChannels];
                    for (int c = 0; c < channels; ++c)
                        pointers[c] = buffer.getWritePointer (c, (int) (from - (start - margin)));
                    input.read (pointers, channels, from, (int) (to - from));

                    const auto valid = juce::Range<int> ((int) (from - (start - margin)), (int) (to - (start - margin)));
                    for (int c = 0; c < channels; ++c)
                        repairBlock (buffer.getWritePointer (c), c, valid, juce::Range<int> (margin, margin + n));

                    const float* read[maxChannels];
                    for (int c = 0; c < channels; ++c)
                        read[c] = buffer.getReadPointer (c, margin);

                    if (! out.append (read, n))
                    {
                        out.finish();
                        return {};
                    }

                    control.report ((double) (start + n) / (double) juce::jmax ((juce::int64) 1, length));
                }

                return out.finish();
            }

            /** interpolate() on a window of `context` samples around the gap only, so
                the result does not depend on where the block starts. */
            bool interpolateLocally (float* x, juce::Range<int> valid, int gapStart, int gapLength, int order, int context)
            {
                const auto from = juce::jmax (valid.getStart(), gapStart - context);
                const auto to = juce::jmin (valid.getEnd(), gapStart + gapLength + context);
                return interpolate (x + from, to - from, gapStart - from, gapLength, order);
            }

            float medianAbs (std::vector<float>& values)
            {
                if (values.empty())
                    return 0.0f;
                const auto mid = values.begin() + (std::ptrdiff_t) (values.size() / 2);
                std::nth_element (values.begin(), mid, values.end());
                return *mid;
            }
        }

        AudioSequence removeClicks (const AudioSequence& input, float sensitivity, float maxClickMs,
                                    BlockStore& store, const WorkControl& control, Report* report)
        {
            const auto rate = input.getSampleRate();
            constexpr int order = 24, blockSize = 4096, margin = 512;
            const auto maxGap = juce::jlimit (1, 400, (int) std::lround (maxClickMs * 0.001 * rate));
            // Sensitivity 0 -> a click must stand out 20 times above the usual error, 100 -> 4 times.
            const auto factor = 20.0f - 16.0f * juce::jlimit (0.0f, 100.0f, sensitivity) / 100.0f;
            int repaired = 0;

            std::vector<float> residual, magnitudes;
            std::vector<bool> marked;

            auto result = processBlocks (input, blockSize, margin, store, control,
                                         [&] (float* x, int, juce::Range<int> valid, juce::Range<int> core)
            {
                const int length = blockSize + 2 * margin;
                const auto a = predictionCoefficients (x + valid.getStart(), valid.getLength(), order);

                // Prediction error: large where the waveform suddenly does something unpredictable.
                residual.assign ((size_t) length, 0.0f);
                magnitudes.clear();
                for (int t = valid.getStart() + order; t < valid.getEnd(); ++t)
                {
                    double e = 0.0;
                    for (int i = 0; i <= order; ++i)
                        e += a[(size_t) i] * x[t - i];
                    residual[(size_t) t] = (float) e;
                    magnitudes.push_back (std::abs ((float) e));
                }

                const auto typical = medianAbs (magnitudes) * 1.4826f;
                if (typical <= 1.0e-7f)
                    return;

                const auto limit = typical * factor;
                marked.assign ((size_t) length, false);
                for (int t = valid.getStart() + order; t < valid.getEnd(); ++t)
                    if (std::abs (residual[(size_t) t]) > limit)
                        marked[(size_t) t] = true;

                // Join marks into gaps and repair the ones that start in the core of the block.
                int t = core.getStart() - maxGap;
                t = juce::jmax (t, valid.getStart() + order);
                while (t < core.getEnd())
                {
                    if (! marked[(size_t) t])
                    {
                        ++t;
                        continue;
                    }

                    int end = t + 1;
                    for (int quiet = 0; end < valid.getEnd() && quiet < 8; ++end)
                        quiet = marked[(size_t) end] ? 0 : quiet + 1;
                    end -= 8;

                    // The error shows up where a click starts and lasts until the
                    // model has "forgotten" it: repair a little before and after.
                    const int gapStart = t - 2;
                    const int gapLength = end - t + 4;

                    // Gaps that reach into the core of the block are repaired here (the
                    // same way in the neighbouring block, so the halves match).
                    if (gapStart < core.getEnd() && gapStart + gapLength > core.getStart() && gapLength <= maxGap
                        && interpolateLocally (x, valid, gapStart, gapLength, order, 256)
                        && gapStart >= core.getStart())
                        ++repaired;

                    t = end + 8;
                }
            });

            if (report != nullptr)
                report->repaired = repaired;

            return result;
        }

        AudioSequence repairClipping (const AudioSequence& input, float tolerancePercent, bool keepLevel,
                                      BlockStore& store, const WorkControl& control, Report* report)
        {
            const auto channels = juce::jmin (input.getNumChannels(), maxChannels);
            const auto length = input.getNumFrames();
            if (channels <= 0)
                return {};

            // 1. The highest peak: what clipped audio is cut off at.
            float peak = 0.0f;
            {
                juce::AudioBuffer<float> buffer (channels, 1 << 16);
                for (juce::int64 pos = 0; pos < length; pos += buffer.getNumSamples())
                {
                    if (control.cancelled())
                        return {};
                    const auto n = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), length - pos);
                    input.read (buffer.getArrayOfWritePointers(), channels, pos, n);
                    for (int c = 0; c < channels; ++c)
                        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, n));
                    control.report (0.2 * (double) (pos + n) / (double) juce::jmax ((juce::int64) 1, length));
                }
            }

            if (peak <= 1.0e-6f)
                return input;

            const auto level = peak * (1.0f - juce::jlimit (0.0f, 20.0f, tolerancePercent) / 100.0f);
            constexpr int order = 32, blockSize = 1 << 15, margin = 2048, maxRun = 800;
            int repaired = 0;
            float newPeak = 0.0f;

            auto rebuilt = processBlocks (input, blockSize, margin, store, control.part (0.2, keepLevel ? 0.6 : 1.0),
                                          [&] (float* x, int, juce::Range<int> valid, juce::Range<int> core)
            {
                int t = juce::jmax (core.getStart() - maxRun, valid.getStart() + order);

                while (t < core.getEnd())
                {
                    if (std::abs (x[t]) < level)
                    {
                        ++t;
                        continue;
                    }

                    const bool positive = x[t] > 0.0f;
                    int end = t + 1;
                    while (end < valid.getEnd() && std::abs (x[end]) >= level && (x[end] > 0.0f) == positive)
                        ++end;

                    const int run = end - t;
                    // A single sample at the peak is normal; two or more in a row are a cut-off peak.
                    if (run >= 2 && run <= maxRun && t < core.getEnd() && end > core.getStart()
                        && interpolateLocally (x, valid, t, run, order, 512))
                    {
                        // The rebuilt peak must not be lower than where it was cut off.
                        for (int i = t; i < end; ++i)
                            x[i] = positive ? juce::jmax (x[i], level) : juce::jmin (x[i], -level);
                        if (t >= core.getStart())
                            ++repaired;
                    }

                    t = end;
                }

                for (int i = core.getStart(); i < core.getEnd(); ++i)
                    newPeak = juce::jmax (newPeak, std::abs (x[i]));
            });

            if (report != nullptr)
                report->repaired = repaired;

            if (rebuilt.getNumChannels() == 0 || ! keepLevel || newPeak <= peak || repaired == 0)
                return rebuilt;

            // 2. Turn it down so the rebuilt peaks fit where the old ones were.
            const auto gain = peak / newPeak;
            if (report != nullptr)
                report->gainDb = 20.0f * std::log10 (gain);

            std::vector<float> gains ((size_t) channels, gain);
            return applyChannelGains (rebuilt, gains, store, control.part (0.6, 1.0));
        }
    }

    //==============================================================================
    namespace
    {
        /** Hum removal: narrow notches at the mains frequency and its harmonics. */
        class HumProcessor final : public EffectProcessor
        {
        public:
            enum { frequency, harmonics, width, depth };
            static constexpr int maxHarmonics = 12, maxChannels = 8;

            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int) override
            {
                rate = sampleRate;
                last = {};
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
                numChannels = juce::jmin (numChannels, maxChannels);
                const Settings now { param (frequency) < 0.5f ? 50.0f : 60.0f,
                                     juce::jlimit (1, maxHarmonics, (int) std::lround (param (harmonics))),
                                     param (width), param (depth) };

                if (! (now == last))
                {
                    last = now;
                    active = 0;
                    for (int h = 1; h <= now.harmonics; ++h)
                    {
                        const double hz = now.baseHz * h;
                        if (hz >= rate * 0.45)
                            break;
                        const auto q = juce::jmax (0.5, hz / juce::jmax (0.1, (double) now.widthHz));
                        const auto k = BiquadCoefficients::peak (rate, hz, q, -now.depthDb);
                        for (auto& ch : filters)
                            ch[(size_t) active].setCoefficients (k);
                        ++active;
                    }
                }

                for (int c = 0; c < numChannels; ++c)
                    for (int h = 0; h < active; ++h)
                    {
                        auto& f = filters[(size_t) c][(size_t) h];
                        f.process (data[c], numFrames);
                        f.sanitise();
                    }
            }

        private:
            struct Settings
            {
                float baseHz = 0.0f;
                int harmonics = 0;
                float widthHz = 0.0f, depthDb = 0.0f;

                bool operator== (const Settings& o) const
                {
                    return baseHz == o.baseHz && harmonics == o.harmonics && widthHz == o.widthHz && depthDb == o.depthDb;
                }
            };

            double rate = 48000.0;
            Settings last;
            int active = 0;
            std::array<std::array<Biquad, maxHarmonics>, maxChannels> filters;
        };
    }

    namespace builtin
    {
        EffectType humRemoval()
        {
            using P = ParameterInfo;
            EffectType t;
            t.id = "humRemoval";
            t.name = "Hum Removal";
            t.category = "Noise Reduction and Repair";
            t.description = "Remove mains hum (50 or 60 Hz) and its harmonics";
            t.parameters = {
                P::choice ("frequency", "Mains frequency", { "50 Hz (Europe, Turkey)", "60 Hz (America)" }, 0),
                P::number ("harmonics", "Harmonics", 1.0f, 12.0f, 6.0f, "", 0),
                P::number ("width", "Notch width", 0.5f, 20.0f, 3.0f, "Hz", 1, 3.0f),
                P::number ("depth", "Reduction", 6.0f, 60.0f, 40.0f, "dB", 0),
            };
            t.factoryPresets = {
                { "50 Hz hum",           { 0, 6, 3, 40 } },
                { "60 Hz hum",           { 1, 6, 3, 40 } },
                { "50 Hz buzz (strong)", { 0, 12, 4, 50 } },
            };
            t.response = [] (const std::vector<float>& v, double sampleRate, double hz)
            {
                const auto base = v.size() > 0 && v[0] > 0.5f ? 60.0 : 50.0;
                const auto count = v.size() > 1 ? juce::jlimit (1, 12, (int) std::lround (v[1])) : 6;
                const auto widthHz = v.size() > 2 ? (double) v[2] : 3.0;
                const auto depthDb = v.size() > 3 ? (double) v[3] : 40.0;
                double gain = 1.0;
                for (int h = 1; h <= count && base * h < sampleRate * 0.45; ++h)
                    gain *= BiquadCoefficients::peak (sampleRate, base * h, juce::jmax (0.5, base * h / juce::jmax (0.1, widthHz)), -depthDb)
                                .magnitude (sampleRate, hz);
                return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, gain)));
            };
            t.create = [] (ParameterValuesPtr v) { return std::make_unique<HumProcessor> (std::move (v)); };
            return t;
        }

        EffectType clickRemoval()
        {
            using P = ParameterInfo;
            EffectType t;
            t.id = "clickRemoval";
            t.name = "Click Removal";
            t.category = "Noise Reduction and Repair";
            t.description = "Find and remove clicks, pops and crackle (vinyl, mouth clicks, digital errors)";
            t.kind = EffectType::Kind::offline;
            t.parameters = {
                P::number ("sensitivity", "Sensitivity", 0.0f, 100.0f, 50.0f, "%", 0),
                P::number ("maxLength", "Longest click", 0.2f, 8.0f, 2.0f, "ms"),
            };
            t.factoryPresets = {
                { "Mouth clicks",    { 45, 1.5f } },
                { "Vinyl crackle",   { 65, 1.0f } },
                { "Loud pops",       { 30, 6.0f } },
            };
            return t;
        }

        EffectType clipRepair()
        {
            using P = ParameterInfo;
            EffectType t;
            t.id = "clipRepair";
            t.name = "Clip Repair";
            t.category = "Noise Reduction and Repair";
            t.description = "Rebuild peaks that were cut off because the recording was too loud";
            t.kind = EffectType::Kind::offline;
            t.parameters = {
                P::number ("tolerance", "Clip level tolerance", 0.0f, 10.0f, 1.0f, "%"),
                P::toggle ("keepLevel", "Lower the level so the rebuilt peaks fit", true),
            };
            t.factoryPresets = {
                { "Default",         { 1.0f, 1 } },
                { "Soft clipping",   { 4.0f, 1 } },
            };
            return t;
        }
    }
}
