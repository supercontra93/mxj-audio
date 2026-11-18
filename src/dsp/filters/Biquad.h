// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cmath>
#include <complex>

namespace mxj::dsp
{
    /** Coefficients of a second order IIR filter, normalised so that a0 = 1.
        Designs from Robert Bristow-Johnson's "Audio EQ Cookbook" (public domain
        formulas). Double precision, so low frequencies stay accurate. */
    struct BiquadCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        static BiquadCoefficients lowPass   (double sampleRate, double hz, double q);
        static BiquadCoefficients highPass  (double sampleRate, double hz, double q);
        static BiquadCoefficients bandPass  (double sampleRate, double hz, double q);
        static BiquadCoefficients peak      (double sampleRate, double hz, double q, double gainDb);
        static BiquadCoefficients lowShelf  (double sampleRate, double hz, double q, double gainDb);
        static BiquadCoefficients highShelf (double sampleRate, double hz, double q, double gainDb);
        static BiquadCoefficients allPass   (double sampleRate, double hz, double q);

        /** First order filters (b2 = a2 = 0). */
        static BiquadCoefficients lowPass1  (double sampleRate, double hz);
        static BiquadCoefficients highPass1 (double sampleRate, double hz);

        /** Magnitude of the response at `hz`, as a gain factor. */
        double magnitude (double sampleRate, double hz) const;
    };

    /** One channel of a biquad filter (transposed direct form II). Real-time safe. */
    class Biquad
    {
    public:
        void setCoefficients (const BiquadCoefficients& c) noexcept { k = c; }
        const BiquadCoefficients& getCoefficients() const noexcept { return k; }

        void reset() noexcept { z1 = z2 = 0.0; }

        inline float processSample (float x) noexcept
        {
            const double in = x;
            const double out = k.b0 * in + z1;
            z1 = k.b1 * in - k.a1 * out + z2;
            z2 = k.b2 * in - k.a2 * out;
            return (float) out;
        }

        void process (float* data, int numFrames) noexcept
        {
            for (int i = 0; i < numFrames; ++i)
                data[i] = processSample (data[i]);
        }

        /** Flushes denormals and stops the state from blowing up after NaNs. */
        void sanitise() noexcept
        {
            if (! std::isfinite (z1) || ! std::isfinite (z2)) { z1 = z2 = 0.0; }
            if (std::abs (z1) < 1.0e-20) z1 = 0.0;
            if (std::abs (z2) < 1.0e-20) z2 = 0.0;
        }

    private:
        BiquadCoefficients k;
        double z1 = 0.0, z2 = 0.0;
    };
}
