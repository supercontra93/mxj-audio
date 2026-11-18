// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/process/Analysis.h"
#include "dsp/analysis/Loudness.h"
#include "core/audio/DerivedSources.h"

#include <cmath>

namespace mxj::dsp
{
    AudioStats analyse (const AudioSequence& audio, const WorkControl& control)
    {
        AudioStats stats;
        const auto channels = audio.getNumChannels();
        const auto length = audio.getNumFrames();
        stats.numChannels = channels;
        stats.numFrames = length;
        stats.sampleRate = audio.getSampleRate();

        if (channels <= 0)
            return stats;

        constexpr int chunk = 1 << 14;
        juce::AudioBuffer<float> buffer (channels, chunk);

        auto loudness = std::make_unique<LoudnessMeter>();
        auto truePeak = std::make_unique<TruePeakDetector>();
        loudness->prepare (audio.getSampleRate(), channels);
        truePeak->prepare (channels);

        std::vector<float> peaks ((size_t) channels, 0.0f);
        std::vector<double> sumSquares ((size_t) channels, 0.0), sums ((size_t) channels, 0.0);
        stats.channelClipped.assign ((size_t) channels, 0);

        for (juce::int64 pos = 0; pos < length; pos += chunk)
        {
            if (control.cancelled())
                return {};

            const auto n = (int) juce::jmin ((juce::int64) chunk, length - pos);
            audio.read (buffer.getArrayOfWritePointers(), channels, pos, n);

            for (int c = 0; c < channels; ++c)
            {
                const auto* d = buffer.getReadPointer (c);
                double sq = 0.0, s = 0.0;
                float p = peaks[(size_t) c];
                juce::int64 clipped = 0;

                for (int i = 0; i < n; ++i)
                {
                    const auto x = d[i];
                    const auto a = std::abs (x);
                    p = juce::jmax (p, a);
                    sq += (double) x * x;
                    s += x;
                    if (a >= 0.9999f)
                        ++clipped;
                }

                peaks[(size_t) c] = p;
                sumSquares[(size_t) c] += sq;
                sums[(size_t) c] += s;
                stats.channelClipped[(size_t) c] += clipped;
            }

            if (channels <= LoudnessMeter::maxChannels)
            {
                loudness->process (buffer.getArrayOfReadPointers(), channels, n);
                truePeak->process (buffer.getArrayOfReadPointers(), channels, n);
            }

            control.report ((double) (pos + n) / (double) juce::jmax ((juce::int64) 1, length));
        }

        double allSquares = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const auto frames = (double) juce::jmax ((juce::int64) 1, length);
            stats.channelPeakDb.push_back (gainToDb (peaks[(size_t) c]));
            stats.channelRmsDb.push_back (gainToDb ((float) std::sqrt (sumSquares[(size_t) c] / frames)));
            stats.channelDcOffset.push_back ((float) (sums[(size_t) c] / frames));
            stats.peakDb = juce::jmax (stats.peakDb, gainToDb (peaks[(size_t) c]));
            stats.truePeakDb = juce::jmax (stats.truePeakDb, gainToDb (truePeak->getPeak (c)));
            allSquares += sumSquares[(size_t) c];
        }

        stats.truePeakDb = juce::jmax (stats.truePeakDb, stats.peakDb);
        stats.rmsDb = gainToDb ((float) std::sqrt (allSquares / ((double) juce::jmax ((juce::int64) 1, length) * channels)));
        stats.integratedLufs = loudness->getIntegrated();
        stats.loudnessRange = loudness->getLoudnessRange();
        stats.maxMomentaryLufs = loudness->getMaxMomentary();
        stats.maxShortTermLufs = loudness->getMaxShortTerm();
        stats.valid = true;
        return stats;
    }

    std::vector<float> normalizeGains (const AudioStats& stats, float peakDb, bool separateChannels)
    {
        std::vector<float> gains;
        if (! stats.valid || stats.peakDb <= -150.0f)
            return gains;

        for (int c = 0; c < stats.numChannels; ++c)
        {
            const auto reference = separateChannels ? stats.channelPeakDb[(size_t) c] : stats.peakDb;
            gains.push_back (reference <= -150.0f ? 1.0f : dbToGain (peakDb - reference));
        }

        return gains;
    }

    //==============================================================================
    LoudnessMatch matchLoudness (const AudioSequence& audio, float target, float peakLimit, bool useLimiter,
                                 BlockStore& store, const WorkControl& control)
    {
        LoudnessMatch result;

        // Progress: analysis 0-30 %, limiter passes 30-100 %.
        auto analysed = [&control] (const AudioSequence& a, double from, double to)
        {
            return analyse (a, control.part (from, to));
        };

        result.before = analysed (audio, 0.0, 0.3);
        if (! result.before.valid)
            return result;

        if (result.before.integratedLufs <= LoudnessMeter::silence + 1.0f)
        {
            // Too short or silent: nothing to measure, leave the audio alone.
            result.audio = audio;
            result.after = result.before;
            return result;
        }

        auto gainDb = target - result.before.integratedLufs;
        const auto peakAfterGain = result.before.truePeakDb + gainDb;

        if (peakAfterGain <= peakLimit || ! useLimiter)
        {
            if (peakAfterGain > peakLimit)
            {
                gainDb -= peakAfterGain - peakLimit; // stay below the peak limit
                result.peakLimitReached = true;
            }

            result.gainDb = gainDb;
            result.audio = derived::gainRamp (audio, dbToGain (gainDb), dbToGain (gainDb), GainRampSource::Curve::linear);
            result.after = analysed (result.audio, 0.3, 1.0);
            return result;
        }

        // Gain plus a limiter under the limit. The limiter works on samples, so the
        // peaks between samples can still pass its ceiling: each pass measures the
        // true peak and lowers the ceiling by what passed, and gives back the
        // loudness the limiter took away.
        result.limited = true;

        auto runPass = [&] (float gain, float ceiling, double from, double to)
        {
            ChainSettings chain;
            auto amp = EffectSettings::defaultsFor ("amplify");
            amp.values[0] = gain;
            chain.push_back (amp);

            auto lim = EffectSettings::defaultsFor ("limiter");
            lim.values[0] = 0.0f;     // input gain
            lim.values[1] = ceiling;
            lim.values[2] = 80.0f;    // release
            chain.push_back (lim);

            auto processors = ProcessorChain::create (chain);
            return render (audio, *processors, store, {}, control.part (from, to));
        };

        constexpr int maxPasses = 4;
        auto ceiling = peakLimit - 0.5f;

        for (int pass = 0; pass < maxPasses; ++pass)
        {
            const auto from = 0.3 + 0.7 * pass / maxPasses;
            const auto step = 0.7 / maxPasses;

            auto processed = runPass (gainDb, ceiling, from, from + step * 0.7);
            if (processed.getNumChannels() == 0)
                return result;

            const auto stats = analysed (processed, from + step * 0.7, from + step);
            if (! stats.valid)
                return result;

            result.gainDb = gainDb;
            result.audio = processed;
            result.after = stats;

            const auto peakOver = stats.truePeakDb - peakLimit;
            const auto correction = target - stats.integratedLufs;
            if (peakOver <= 0.0f && std::abs (correction) < 0.1f)
                break;

            if (peakOver > 0.0f)
                ceiling -= peakOver + 0.1f;
            gainDb += correction;
        }

        result.peakLimitReached = result.after.truePeakDb > peakLimit;
        return result;
    }
}
