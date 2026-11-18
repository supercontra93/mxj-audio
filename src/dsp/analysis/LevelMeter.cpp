// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/analysis/LevelMeter.h"
#include "dsp/effects/Parameters.h"

#include <cmath>

namespace mxj::dsp
{
    void LevelMeter::prepare (double sampleRate, int numChannels)
    {
        channels = juce::jlimit (1, maxChannels, numChannels);
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        rmsCoeff = std::exp (-1.0 / (rate * 0.3));
        correlationCoeff = std::exp (-1.0 / (rate * 0.3));
        loudness.prepare (rate, channels);
        truePeakDetector.prepare (channels);
        reset();
    }

    void LevelMeter::reset()
    {
        loudness.reset();
        truePeakDetector.reset();
        meanSquare.fill (0.0);
        sumLR = sumLL = sumRR = 0.0;

        for (int c = 0; c < maxChannels; ++c)
        {
            peak[(size_t) c] = 0.0f;
            truePeak[(size_t) c] = 0.0f;
            rms[(size_t) c] = 0.0f;
        }

        correlation = 1.0f;
        maxTruePeakDb = -160.0f;
    }

    void LevelMeter::process (const float* const* data, int numChannels, int numFrames)
    {
        if (resetRequested.exchange (false))
            reset();

        numChannels = juce::jmin (numChannels, channels);
        if (numChannels <= 0 || numFrames <= 0)
            return;

        for (int c = 0; c < numChannels; ++c)
        {
            float p = 0.0f;
            auto ms = meanSquare[(size_t) c];
            for (int i = 0; i < numFrames; ++i)
            {
                const auto x = data[c][i];
                p = juce::jmax (p, std::abs (x));
                ms = rmsCoeff * ms + (1.0 - rmsCoeff) * (double) x * x;
            }

            meanSquare[(size_t) c] = ms < 1.0e-20 ? 0.0 : ms;
            rms[(size_t) c].store ((float) std::sqrt (meanSquare[(size_t) c]), std::memory_order_relaxed);

            if (p > peak[(size_t) c].load (std::memory_order_relaxed))
                peak[(size_t) c].store (p, std::memory_order_relaxed);
        }

        if (numChannels == 2)
        {
            const auto k = correlationCoeff;
            for (int i = 0; i < numFrames; ++i)
            {
                const double l = data[0][i], r = data[1][i];
                sumLR = k * sumLR + (1.0 - k) * l * r;
                sumLL = k * sumLL + (1.0 - k) * l * l;
                sumRR = k * sumRR + (1.0 - k) * r * r;
            }

            const auto denominator = std::sqrt (sumLL * sumRR);
            correlation.store (denominator > 1.0e-10 ? (float) juce::jlimit (-1.0, 1.0, sumLR / denominator) : 1.0f,
                               std::memory_order_relaxed);
        }

        truePeakDetector.process (data, numChannels, numFrames);
        float highest = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            highest = juce::jmax (highest, truePeakDetector.getPeak (c));
            const auto tp = truePeakDetector.getLastPeak (c);
            if (tp > truePeak[(size_t) c].load (std::memory_order_relaxed))
                truePeak[(size_t) c].store (tp, std::memory_order_relaxed);
        }

        maxTruePeakDb.store (gainToDb (highest), std::memory_order_relaxed);
        loudness.process (data, numChannels, numFrames);
    }

    float LevelMeter::takePeak (int channel) noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return 0.0f;

        return peak[(size_t) channel].exchange (0.0f);
    }

    float LevelMeter::takeTruePeak (int channel) noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return 0.0f;

        return truePeak[(size_t) channel].exchange (0.0f);
    }

    float LevelMeter::getRms (int channel) const noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return 0.0f;

        return rms[(size_t) channel].load (std::memory_order_relaxed);
    }
}
