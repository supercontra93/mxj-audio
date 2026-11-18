// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/analysis/Loudness.h"

#include <cmath>

namespace mxj::dsp
{
    std::array<BiquadCoefficients, 2> LoudnessMeter::kWeighting (double sampleRate)
    {
        // The two stages of BS.1770, computed for any sample rate from their
        // analogue prototypes (the same derivation as libebur128, MIT licence).
        const double pi = 3.14159265358979323846;
        std::array<BiquadCoefficients, 2> k;

        {
            const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
            const double kk = std::tan (pi * f0 / sampleRate);
            const double vh = std::pow (10.0, g / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + kk / q + kk * kk;
            k[0] = { (vh + vb * kk / q + kk * kk) / a0, 2.0 * (kk * kk - vh) / a0, (vh - vb * kk / q + kk * kk) / a0,
                     2.0 * (kk * kk - 1.0) / a0, (1.0 - kk / q + kk * kk) / a0 };
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double kk = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + kk / q + kk * kk;
            k[1] = { 1.0, -2.0, 1.0, 2.0 * (kk * kk - 1.0) / a0, (1.0 - kk / q + kk * kk) / a0 };
        }

        return k;
    }

    float LoudnessMeter::toLufs (double meanSquare) noexcept
    {
        if (meanSquare <= 1.0e-12)
            return silence;

        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    void LoudnessMeter::Histogram::add (double meanSquare, float lufs)
    {
        if (lufs <= lowest)
            return;

        const auto bin = juce::jlimit (0, numBins - 1, (int) ((lufs - lowest) * 10.0f));
        energy[(size_t) bin] += meanSquare;
        ++count[(size_t) bin];
    }

    void LoudnessMeter::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        channels = juce::jlimit (1, maxChannels, numChannels);
        subBlockFrames = juce::jmax (1, (int) std::lround (rate * 0.1));

        const auto k = kWeighting (rate);
        for (auto& ch : filters)
        {
            ch[0].setCoefficients (k[0]);
            ch[1].setCoefficients (k[1]);
        }

        reset();
    }

    void LoudnessMeter::reset()
    {
        for (auto& ch : filters)
            for (auto& f : ch)
                f.reset();

        subBlockSum = 0.0;
        framesInSubBlock = 0;
        subBlocks.fill (0.0);
        subBlockWrite = 0;
        subBlocksDone = 0;
        gatingBlocks.clear();
        shortTermBlocks.clear();

        momentary = silence;
        shortTerm = silence;
        integrated = silence;
        range = 0.0f;
        maxMomentary = silence;
        maxShortTerm = silence;
    }

    void LoudnessMeter::process (const float* const* data, int numChannels, int numFrames)
    {
        numChannels = juce::jmin (numChannels, channels);

        for (int i = 0; i < numFrames; ++i)
        {
            double sum = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                auto& f = filters[(size_t) c];
                const double y = f[1].processSample (f[0].processSample (data[c][i]));
                sum += y * y;
            }

            subBlockSum += sum;

            if (++framesInSubBlock >= subBlockFrames)
                subBlockFinished();
        }

        for (auto& ch : filters)
            for (auto& f : ch)
                f.sanitise();
    }

    void LoudnessMeter::subBlockFinished()
    {
        subBlocks[(size_t) subBlockWrite] = subBlockSum / (double) subBlockFrames;
        subBlockWrite = (subBlockWrite + 1) % (int) subBlocks.size();
        subBlockSum = 0.0;
        framesInSubBlock = 0;
        ++subBlocksDone;

        auto meanOfLast = [this] (int n)
        {
            double s = 0.0;
            for (int i = 1; i <= n; ++i)
                s += subBlocks[(size_t) ((subBlockWrite - i + (int) subBlocks.size()) % (int) subBlocks.size())];
            return s / n;
        };

        if (subBlocksDone >= 4)
        {
            const auto ms = meanOfLast (4);
            const auto l = toLufs (ms);
            momentary = l;
            if (l > maxMomentary.load())
                maxMomentary = l;

            gatingBlocks.add (ms, l);
            updateIntegrated();
        }

        if (subBlocksDone >= 30)
        {
            const auto ms = meanOfLast (30);
            const auto l = toLufs (ms);
            shortTerm = l;
            if (l > maxShortTerm.load())
                maxShortTerm = l;

            shortTermBlocks.add (ms, l);
            updateRange();
        }
    }

    void LoudnessMeter::updateIntegrated()
    {
        // Absolute gate (-70 LUFS) is built into the histogram; then the relative gate 10 LU below.
        double energy = 0.0;
        juce::uint64 count = 0;
        for (int b = 0; b < numBins; ++b)
        {
            energy += gatingBlocks.energy[(size_t) b];
            count += gatingBlocks.count[(size_t) b];
        }

        if (count == 0)
        {
            integrated = silence;
            return;
        }

        const auto gate = toLufs (energy / (double) count) - 10.0f;
        const auto firstBin = juce::jlimit (0, numBins, (int) std::ceil ((gate - lowest) * 10.0f));

        energy = 0.0;
        count = 0;
        for (int b = firstBin; b < numBins; ++b)
        {
            energy += gatingBlocks.energy[(size_t) b];
            count += gatingBlocks.count[(size_t) b];
        }

        integrated = count > 0 ? toLufs (energy / (double) count) : silence;
    }

    void LoudnessMeter::updateRange()
    {
        // EBU Tech 3342: short-term values, absolute gate -70, relative gate 20 LU
        // below their mean, then the spread between the 10th and 95th percentile.
        double energy = 0.0;
        juce::uint64 count = 0;
        for (int b = 0; b < numBins; ++b)
        {
            energy += shortTermBlocks.energy[(size_t) b];
            count += shortTermBlocks.count[(size_t) b];
        }

        if (count == 0)
        {
            range = 0.0f;
            return;
        }

        const auto gate = toLufs (energy / (double) count) - 20.0f;
        const auto firstBin = juce::jlimit (0, numBins, (int) std::ceil ((gate - lowest) * 10.0f));

        juce::uint64 total = 0;
        for (int b = firstBin; b < numBins; ++b)
            total += shortTermBlocks.count[(size_t) b];

        if (total == 0)
        {
            range = 0.0f;
            return;
        }

        auto percentile = [&] (double p)
        {
            const auto wanted = (juce::uint64) std::ceil (p * (double) total);
            juce::uint64 seen = 0;
            for (int b = firstBin; b < numBins; ++b)
            {
                seen += shortTermBlocks.count[(size_t) b];
                if (seen >= juce::jmax ((juce::uint64) 1, wanted))
                    return lowest + (b + 0.5f) * 0.1f;
            }
            return lowest + numBins * 0.1f;
        };

        range = juce::jmax (0.0f, percentile (0.95) - percentile (0.10));
    }

    //==============================================================================
    void TruePeakDetector::prepare (int numChannels)
    {
        channels = juce::jlimit (1, maxChannels, numChannels);

        // Windowed sinc for 4x interpolation, cut off at the original Nyquist frequency.
        const int n = phases * tapsPerPhase;
        const double centre = (n - 1) / 2.0;
        std::array<double, phases * tapsPerPhase> h {};

        for (int i = 0; i < n; ++i)
        {
            const auto x = (i - centre) / phases;
            const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const auto w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i + 0.5) / n)
                                + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * (i + 0.5) / n); // Blackman
            h[(size_t) i] = sinc * w;
        }

        // Normalise each phase to unity gain at DC, so a constant signal reads exactly.
        for (int p = 0; p < phases; ++p)
        {
            double sum = 0.0;
            for (int t = 0; t < tapsPerPhase; ++t)
                sum += h[(size_t) (t * phases + p)];

            for (int t = 0; t < tapsPerPhase; ++t)
                coefficients[0][(size_t) (p * tapsPerPhase + t)] = (float) (h[(size_t) (t * phases + p)] / sum);
        }

        reset();
    }

    void TruePeakDetector::reset()
    {
        for (auto& h : history)
            h.fill (0.0f);
        historyPos.fill (0);
        peaks.fill (0.0f);
        lastPeaks.fill (0.0f);
    }

    float TruePeakDetector::process (const float* const* data, int numChannels, int numFrames)
    {
        numChannels = juce::jmin (numChannels, channels);
        float blockPeak = 0.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& h = history[(size_t) c];
            auto pos = historyPos[(size_t) c];
            float peak = peaks[(size_t) c];
            float localPeak = 0.0f;

            for (int i = 0; i < numFrames; ++i)
            {
                // History kept twice so the newest tapsPerPhase samples are always contiguous.
                pos = pos == 0 ? tapsPerPhase - 1 : pos - 1;
                h[(size_t) pos] = h[(size_t) (pos + tapsPerPhase)] = data[c][i];
                const auto* x = h.data() + pos;

                for (int p = 0; p < phases; ++p)
                {
                    const auto* k = coefficients[0].data() + p * tapsPerPhase;
                    float y = 0.0f;
                    for (int t = 0; t < tapsPerPhase; ++t)
                        y += k[t] * x[t];

                    localPeak = juce::jmax (localPeak, std::abs (y));
                }

                localPeak = juce::jmax (localPeak, std::abs (data[c][i]));
            }

            historyPos[(size_t) c] = pos;
            peaks[(size_t) c] = juce::jmax (peak, localPeak);
            lastPeaks[(size_t) c] = localPeak;
            blockPeak = juce::jmax (blockPeak, localPeak);
        }

        return blockPeak;
    }
}
