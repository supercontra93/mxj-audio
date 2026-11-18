// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/analysis/Loudness.h"

namespace mxj::dsp
{
    /** Everything the meter panel shows about the audio that is playing: peak,
        RMS and true peak per channel, phase correlation and loudness.

        process() runs on the audio thread (real-time safe after prepare());
        the interface reads the atomics from a timer. Peak and true peak hold the
        highest value until the interface takes it with takePeak(), so short
        peaks are never missed between two screen updates.
    */
    class LevelMeter
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate, int numChannels);

        /** Clears the loudness measurement and the peak holds. Real-time safe;
            call it from the thread that processes. */
        void reset();

        /** Asks the processing thread to reset before its next block. Any thread. */
        void requestReset() noexcept { resetRequested = true; }

        void process (const float* const* channels, int numChannels, int numFrames);

        /** Highest sample since the last call (gain). Any thread. */
        float takePeak (int channel) noexcept;
        float takeTruePeak (int channel) noexcept;

        /** RMS over the last 300 ms (gain). */
        float getRms (int channel) const noexcept;

        /** +1 = the same on both sides (mono), 0 = unrelated, -1 = opposite (phase problem). */
        float getCorrelation() const noexcept { return correlation.load (std::memory_order_relaxed); }

        /** Highest true peak since reset(), in dBTP. */
        float getMaxTruePeakDb() const noexcept { return maxTruePeakDb.load (std::memory_order_relaxed); }

        const LoudnessMeter& getLoudness() const noexcept { return loudness; }

        int getNumChannels() const noexcept { return channels; }

    private:
        int channels = 2;
        double rmsCoeff = 0.999, correlationCoeff = 0.999;
        std::array<double, maxChannels> meanSquare {};
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

        std::array<std::atomic<float>, maxChannels> peak {}, truePeak {}, rms {};
        std::atomic<float> correlation { 1.0f }, maxTruePeakDb { -160.0f };
        std::atomic<bool> resetRequested { false };

        LoudnessMeter loudness;
        TruePeakDetector truePeakDetector;
    };
}
