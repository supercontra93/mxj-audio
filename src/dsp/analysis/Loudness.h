// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/filters/Biquad.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>

namespace mxj::dsp
{
    /** Loudness after ITU-R BS.1770-4 and EBU R128 / Tech 3341 and 3342:
        momentary (400 ms), short-term (3 s), integrated (gated) loudness in
        LUFS and loudness range (LRA) in LU.

        Real-time safe after prepare(): process() runs on the audio thread for
        the meters and on a worker for file analysis. The read-outs are atomics,
        so any thread may read them while another processes.

        Every channel has weight 1 (mono, stereo; surround weighting comes with
        multichannel support). Integrated loudness and LRA use histograms with
        0.1 LU steps, so they need no memory however long the audio is.
    */
    class LoudnessMeter
    {
    public:
        static constexpr int maxChannels = 8;

        /** "No value yet": below the -70 LUFS absolute gate, or not enough audio. */
        static constexpr float silence = -100.0f;

        void prepare (double sampleRate, int numChannels);

        /** Forgets everything measured so far. Real-time safe. */
        void reset();

        void process (const float* const* channels, int numChannels, int numFrames);

        float getMomentary() const noexcept      { return momentary.load (std::memory_order_relaxed); }
        float getShortTerm() const noexcept      { return shortTerm.load (std::memory_order_relaxed); }
        float getIntegrated() const noexcept     { return integrated.load (std::memory_order_relaxed); }
        float getLoudnessRange() const noexcept  { return range.load (std::memory_order_relaxed); }
        float getMaxMomentary() const noexcept   { return maxMomentary.load (std::memory_order_relaxed); }
        float getMaxShortTerm() const noexcept   { return maxShortTerm.load (std::memory_order_relaxed); }

        /** The K-weighting filter (pre-filter and RLB high-pass) for a sample rate. */
        static std::array<BiquadCoefficients, 2> kWeighting (double sampleRate);

    private:
        static constexpr int numBins = 850;        ///< -70 ... +15 LUFS in 0.1 LU steps
        static constexpr float lowest = -70.0f;

        struct Histogram
        {
            std::array<double, numBins> energy {};
            std::array<juce::uint32, numBins> count {};

            void clear() { energy.fill (0.0); count.fill (0); }
            void add (double meanSquare, float lufs);
        };

        void subBlockFinished();
        void updateIntegrated();
        void updateRange();

        static float toLufs (double meanSquare) noexcept;

        double rate = 48000.0;
        int channels = 2, subBlockFrames = 4800, framesInSubBlock = 0;
        std::array<std::array<Biquad, 2>, maxChannels> filters {};
        double subBlockSum = 0.0;
        std::array<double, 30> subBlocks {};   ///< mean squares of the last 30 sub-blocks (100 ms each)
        int subBlockWrite = 0;
        juce::int64 subBlocksDone = 0;

        Histogram gatingBlocks, shortTermBlocks;

        std::atomic<float> momentary { silence }, shortTerm { silence }, integrated { silence },
                           range { 0.0f }, maxMomentary { silence }, maxShortTerm { silence };
    };

    //==============================================================================
    /** True peak (inter-sample peak) after ITU-R BS.1770-4 annex 2: 4x
        oversampling with a 48 tap interpolation filter. Real-time safe after prepare(). */
    class TruePeakDetector
    {
    public:
        static constexpr int maxChannels = 8;

        void prepare (int numChannels);
        void reset();

        /** Processes frames and returns the highest true peak (gain, not dB) among them. */
        float process (const float* const* channels, int numChannels, int numFrames);

        /** Highest true peak per channel since reset(). */
        float getPeak (int channel) const noexcept { return channel >= 0 && channel < maxChannels ? peaks[(size_t) channel] : 0.0f; }

        /** Highest true peak per channel in the last process() call. */
        float getLastPeak (int channel) const noexcept { return channel >= 0 && channel < maxChannels ? lastPeaks[(size_t) channel] : 0.0f; }

    private:
        static constexpr int phases = 4, tapsPerPhase = 12;
        int channels = 2;
        std::array<std::array<float, phases * tapsPerPhase>, 1> coefficients {};
        std::array<std::array<float, tapsPerPhase * 2>, maxChannels> history {};
        std::array<int, maxChannels> historyPos {};
        std::array<float, maxChannels> peaks {}, lastPeaks {};
    };
}
