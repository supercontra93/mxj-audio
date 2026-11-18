// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/effects/Effect.h"
#include "dsp/process/Render.h"

#include <memory>
#include <mutex>

namespace mxj::dsp
{
    /** The sound of the background noise alone, measured from a selection that
        holds only noise ("noise print", as in Audition). Power per frequency
        (|X|^2 / window energy, so it does not depend on the window size). */
    struct NoiseProfile
    {
        double sampleRate = 0.0;
        int fftSize = 0;
        std::vector<std::vector<float>> power; ///< per channel, per bin (fftSize / 2 + 1)
        double seconds = 0.0;                  ///< length of the audio it was taken from

        bool isValid() const noexcept { return sampleRate > 0.0 && fftSize > 0 && ! power.empty(); }

        /** Noise power at a frequency (interpolated), for a channel (the last one for higher channels). */
        float powerAt (int channel, double hz) const;

        /** Measures the noise of `audio`. Background thread. Returns an invalid profile if cancelled. */
        static NoiseProfile capture (const AudioSequence& audio, const WorkControl& control = {});
    };

    /** The noise print the Noise Reduction effect uses. The app captures it;
        processors pick it up in prepare(). Thread safe (never used on the audio thread). */
    namespace noisePrint
    {
        void set (std::shared_ptr<const NoiseProfile>);
        std::shared_ptr<const NoiseProfile> get();
    }
}
