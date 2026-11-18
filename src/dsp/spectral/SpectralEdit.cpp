// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/spectral/SpectralEdit.h"

#include <cmath>

namespace mxj::dsp::spectral
{
    namespace
    {
        int windowSizeFor (const AudioSequence& audio) { return 1 << stft::orderForRate (audio.getSampleRate()); }

        /** A fixed pseudo random phase per frame and bin, so filling a hole gives
            the same result when a window is analysed twice. */
        std::complex<float> randomPhase (juce::int64 centre, int bin, int channel)
        {
            auto h = (juce::uint64) centre * 0x9E3779B97F4A7C15ull ^ (juce::uint64) (bin + 1) * 0xC2B2AE3D27D4EB4Full
                   ^ (juce::uint64) (channel + 1) * 0x165667B19E3779F9ull;
            h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;
            const auto angle = (float) ((double) (h & 0xffffff) / (double) 0x1000000 * juce::MathConstants<double>::twoPi);
            return std::polar (1.0f, angle);
        }
    }

    float weight (const SpectralRegion& region, juce::int64 centre, double hz, int windowSize, double binHz)
    {
        // Time: full weight inside, fading out over half a window outside.
        const auto fade = (double) windowSize * 0.5;
        double wt = 1.0;
        if (centre < region.time.getStart())
            wt = 1.0 - (double) (region.time.getStart() - centre) / fade;
        else if (centre >= region.time.getEnd())
            wt = 1.0 - (double) (centre - region.time.getEnd() + 1) / fade;

        if (wt <= 0.0)
            return 0.0f;

        if (! region.hasBand())
            return (float) wt;

        // Frequency: soft edges two bins wide.
        const auto edge = 2.0 * binHz;
        double wf = 1.0;
        if (hz < region.lowHz)
            wf = region.lowHz <= 0.0 ? 1.0 : 1.0 - (region.lowHz - hz) / edge;
        else if (hz > region.highHz)
            wf = 1.0 - (hz - region.highHz) / edge;

        return (float) (wt * juce::jlimit (0.0, 1.0, wf));
    }

    FrameRange editRange (const AudioSequence& audio, FrameRange time)
    {
        const auto n = (juce::int64) windowSizeFor (audio);
        return FrameRange (time.getStart() - n, time.getEnd() + n).getIntersectionWith ({ 0, audio.getNumFrames() });
    }

    AudioSequence changeGain (const AudioSequence& audio, const SpectralRegion& region, float gainDb,
                              BlockStore& store, const WorkControl& control)
    {
        const auto order = stft::orderForRate (audio.getSampleRate());
        const int n = 1 << order;
        const auto gain = gainDb <= -100.0f ? 0.0f : std::pow (10.0f, gainDb / 20.0f);

        return stft::process (audio, editRange (audio, region.time), order, [&region, n, gain] (const stft::Frame& f)
        {
            for (int k = 0; k < f.numBins; ++k)
            {
                const auto w = weight (region, f.centre, k * f.binHz, n, f.binHz);
                if (w > 0.0f)
                    f.bins[k] *= 1.0f + (gain - 1.0f) * w;
            }
        }, store, control);
    }

    AudioSequence heal (const AudioSequence& audio, const SpectralRegion& region, BlockStore& store, const WorkControl& control)
    {
        const auto order = stft::orderForRate (audio.getSampleRate());
        const int n = 1 << order;
        const auto rate = audio.getSampleRate();

        // The sound around the region: a quarter second (at least two windows) on each side.
        const auto context = juce::jmax ((juce::int64) (2 * n), (juce::int64) (rate * 0.25));
        const auto start = region.time.getStart(), end = region.time.getEnd();
        const FrameRange beforeRange (juce::jmax ((juce::int64) 0, start - n / 2 - context), juce::jmax ((juce::int64) 0, start - n / 2));
        const FrameRange afterRange (juce::jmin (audio.getNumFrames(), end + n / 2), juce::jmin (audio.getNumFrames(), end + n / 2 + context));

        auto before = stft::averagePower (audio, beforeRange, order, control.part (0.0, 0.05));
        auto after = stft::averagePower (audio, afterRange, order, control.part (0.05, 0.1));
        const bool hasBefore = beforeRange.getLength() >= n / 2, hasAfter = afterRange.getLength() >= n / 2;

        if (control.cancelled() || (! hasBefore && ! hasAfter))
            return {};

        if (! hasBefore) before = after;
        if (! hasAfter)  after = before;

        float energy = 0.0f;
        for (auto w : hannWindow (n))
            energy += w * w;

        const auto length = (double) juce::jmax ((juce::int64) 1, end - start);

        return stft::process (audio, editRange (audio, region.time), order, [&, n, energy, length] (const stft::Frame& f)
        {
            const auto& b = before[(size_t) juce::jmin (f.channel, (int) before.size() - 1)];
            const auto& a = after[(size_t) juce::jmin (f.channel, (int) after.size() - 1)];
            const auto u = (float) juce::jlimit (0.0, 1.0, (double) (f.centre - start) / length);

            for (int k = 0; k < f.numBins; ++k)
            {
                const auto w = weight (region, f.centre, k * f.binHz, n, f.binHz);
                if (w <= 0.0f)
                    continue;

                // Target level: from the level before to the level after, in dB.
                const auto lb = std::log (b[(size_t) k] + 1.0e-20f), la = std::log (a[(size_t) k] + 1.0e-20f);
                const auto target = std::exp (lb + (la - lb) * u);
                const auto power = std::norm (f.bins[k]) / energy;

                if (power > target)
                {
                    // Too loud: lower it to the surrounding level.
                    const auto g = std::sqrt (target / power);
                    f.bins[k] *= std::pow (g, w);
                }
                else if (power * 100.0f < target)
                {
                    // A hole (more than 20 dB below its surroundings): fill it.
                    const auto magnitude = std::sqrt (target * energy);
                    const auto filled = randomPhase (f.centre, k, f.channel) * magnitude;
                    f.bins[k] = f.bins[k] * (1.0f - w) + filled * w;
                }
            }
        }, store, control.part (0.1, 1.0));
    }

    AudioSequence blend (const AudioSequence& original, const AudioSequence& processed, const SpectralRegion& region,
                         BlockStore& store, const WorkControl& control)
    {
        const auto order = stft::orderForRate (original.getSampleRate());
        const int n = 1 << order;

        return stft::process (original, editRange (original, region.time), order, [&region, n] (const stft::Frame& f)
        {
            if (f.other == nullptr)
                return;

            for (int k = 0; k < f.numBins; ++k)
            {
                const auto w = weight (region, f.centre, k * f.binHz, n, f.binHz);
                if (w > 0.0f)
                    f.bins[k] = f.bins[k] * (1.0f - w) + f.other[k] * w;
            }
        }, store, control, &processed);
    }
}
