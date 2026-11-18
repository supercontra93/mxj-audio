// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/filters/Biquad.h"

#include <algorithm>

namespace mxj::dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        struct Prototype
        {
            double w0, cosw, alpha;
        };

        Prototype prototype (double rate, double hz, double q)
        {
            hz = std::clamp (hz, 1.0, rate * 0.4999);
            q = std::max (q, 0.01);
            const auto w0 = 2.0 * pi * hz / rate;
            return { w0, std::cos (w0), std::sin (w0) / (2.0 * q) };
        }

        BiquadCoefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }
    }

    BiquadCoefficients BiquadCoefficients::lowPass (double rate, double hz, double q)
    {
        const auto p = prototype (rate, hz, q);
        return normalise ((1 - p.cosw) / 2, 1 - p.cosw, (1 - p.cosw) / 2, 1 + p.alpha, -2 * p.cosw, 1 - p.alpha);
    }

    BiquadCoefficients BiquadCoefficients::highPass (double rate, double hz, double q)
    {
        const auto p = prototype (rate, hz, q);
        return normalise ((1 + p.cosw) / 2, -(1 + p.cosw), (1 + p.cosw) / 2, 1 + p.alpha, -2 * p.cosw, 1 - p.alpha);
    }

    BiquadCoefficients BiquadCoefficients::bandPass (double rate, double hz, double q)
    {
        // Constant 0 dB peak gain.
        const auto p = prototype (rate, hz, q);
        return normalise (p.alpha, 0, -p.alpha, 1 + p.alpha, -2 * p.cosw, 1 - p.alpha);
    }

    BiquadCoefficients BiquadCoefficients::allPass (double rate, double hz, double q)
    {
        const auto p = prototype (rate, hz, q);
        return normalise (1 - p.alpha, -2 * p.cosw, 1 + p.alpha, 1 + p.alpha, -2 * p.cosw, 1 - p.alpha);
    }

    BiquadCoefficients BiquadCoefficients::peak (double rate, double hz, double q, double gainDb)
    {
        const auto p = prototype (rate, hz, q);
        const auto a = std::pow (10.0, gainDb / 40.0);
        return normalise (1 + p.alpha * a, -2 * p.cosw, 1 - p.alpha * a, 1 + p.alpha / a, -2 * p.cosw, 1 - p.alpha / a);
    }

    BiquadCoefficients BiquadCoefficients::lowShelf (double rate, double hz, double q, double gainDb)
    {
        const auto p = prototype (rate, hz, q);
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto s = 2.0 * std::sqrt (a) * p.alpha;
        return normalise (a * ((a + 1) - (a - 1) * p.cosw + s),
                          2 * a * ((a - 1) - (a + 1) * p.cosw),
                          a * ((a + 1) - (a - 1) * p.cosw - s),
                          (a + 1) + (a - 1) * p.cosw + s,
                          -2 * ((a - 1) + (a + 1) * p.cosw),
                          (a + 1) + (a - 1) * p.cosw - s);
    }

    BiquadCoefficients BiquadCoefficients::highShelf (double rate, double hz, double q, double gainDb)
    {
        const auto p = prototype (rate, hz, q);
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto s = 2.0 * std::sqrt (a) * p.alpha;
        return normalise (a * ((a + 1) + (a - 1) * p.cosw + s),
                          -2 * a * ((a - 1) + (a + 1) * p.cosw),
                          a * ((a + 1) + (a - 1) * p.cosw - s),
                          (a + 1) - (a - 1) * p.cosw + s,
                          2 * ((a - 1) - (a + 1) * p.cosw),
                          (a + 1) - (a - 1) * p.cosw - s);
    }

    BiquadCoefficients BiquadCoefficients::lowPass1 (double rate, double hz)
    {
        hz = std::clamp (hz, 1.0, rate * 0.4999);
        const auto k = std::tan (pi * hz / rate);
        return normalise (k, k, 0, k + 1, k - 1, 0);
    }

    BiquadCoefficients BiquadCoefficients::highPass1 (double rate, double hz)
    {
        hz = std::clamp (hz, 1.0, rate * 0.4999);
        const auto k = std::tan (pi * hz / rate);
        return normalise (1, -1, 0, k + 1, k - 1, 0);
    }

    double BiquadCoefficients::magnitude (double rate, double hz) const
    {
        const auto w = 2.0 * pi * hz / rate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num / den);
    }
}
