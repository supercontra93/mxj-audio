// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/process/Render.h"

namespace mxj::dsp
{
    /** Amplitude statistics and loudness of some audio (master plan, module G:
        "amplitude statistics report"). Levels in dB (dBFS); -160 = silence. */
    struct AudioStats
    {
        bool valid = false;
        int numChannels = 0;
        juce::int64 numFrames = 0;
        double sampleRate = 0.0;

        float integratedLufs = -100.0f;  ///< -100 = too short or too quiet to measure
        float loudnessRange = 0.0f;      ///< LU
        float maxMomentaryLufs = -100.0f;
        float maxShortTermLufs = -100.0f;
        float truePeakDb = -160.0f;      ///< highest of all channels, dBTP
        float peakDb = -160.0f;          ///< highest sample of all channels

        std::vector<float> channelPeakDb, channelRmsDb, channelDcOffset;
        std::vector<juce::int64> channelClipped; ///< samples at or above 0 dBFS (|x| >= 0.9999)

        float rmsDb = -160.0f;           ///< RMS over all channels
    };

    /** Reads the whole sequence once and measures it. Blocks: use a background
        thread. Returns stats with valid = false if cancelled. */
    AudioStats analyse (const AudioSequence& audio, const WorkControl& control = {});

    //==============================================================================
    /** Result of matchLoudness(). */
    struct LoudnessMatch
    {
        AudioSequence audio;         ///< empty if cancelled or failed
        AudioStats before, after;
        float gainDb = 0.0f;         ///< gain applied (before the limiter)
        bool limited = false;        ///< a limiter was needed to respect the true peak limit
        bool peakLimitReached = false; ///< the target was not reached without passing the peak limit (no limiter allowed)
    };

    /** Brings the audio to `targetLufs` integrated loudness.

        If the gain would push the true peak above `truePeakLimitDb`, a limiter
        holds the peaks back (when `useLimiter`), or the gain is reduced so the
        peaks stay below the limit (then the target is not reached). A second
        pass corrects the small loudness loss the limiter causes.

        Simple gains are applied without copying the audio (a gain source);
        limited audio is written into `store`. */
    LoudnessMatch matchLoudness (const AudioSequence& audio, float targetLufs, float truePeakLimitDb, bool useLimiter,
                                 BlockStore& store, const WorkControl& control = {});

    /** Gains that bring the highest peak (of all channels, or of each channel when
        `separateChannels`) to `peakDb`. Empty if the audio is silent. */
    std::vector<float> normalizeGains (const AudioStats& stats, float peakDb, bool separateChannels);
}
