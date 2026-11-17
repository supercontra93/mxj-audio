// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/analysis/Spectrum.h"

#include <algorithm>
#include <cmath>

namespace mxj::dsp
{
    Spectrum::Spectrum (int o) : order (std::clamp (o, 8, 15)), size (1 << order)
    {
        window.resize ((size_t) size);
        buffer.resize ((size_t) size);
        double sum = 0.0;

        for (int i = 0; i < size; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / size));
            sum += window[(size_t) i];
        }

        windowGain = (float) (2.0 / sum); // a sine of amplitude 1 reads as 1 (0 dB)
    }

    void Spectrum::compute (const float* samples, float* magnitudesDb)
    {
        for (int i = 0; i < size; ++i)
            buffer[(size_t) i] = { samples[i] * window[(size_t) i], 0.0f };

        fft (buffer.data(), order);

        for (int i = 0; i < getNumBins(); ++i)
        {
            const auto m = std::abs (buffer[(size_t) i]) * windowGain;
            magnitudesDb[i] = m > 1.0e-9f ? 20.0f * std::log10 (m) : -180.0f;
        }
    }

    void Spectrum::fft (std::complex<float>* data, int fftOrder)
    {
        const int n = 1 << fftOrder;

        // Bit reversal.
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;

            if (i < j)
                std::swap (data[i], data[j]);
        }

        for (int length = 2; length <= n; length <<= 1)
        {
            const auto angle = -2.0 * 3.14159265358979323846 / length;
            const std::complex<float> step ((float) std::cos (angle), (float) std::sin (angle));

            for (int i = 0; i < n; i += length)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (int k = 0; k < length / 2; ++k)
                {
                    const auto u = data[i + k];
                    const auto v = data[i + k + length / 2] * w;
                    data[i + k] = u + v;
                    data[i + k + length / 2] = u - v;
                    w *= step;
                }
            }
        }
    }
}
