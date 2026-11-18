// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/spectral/Fft.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace mxj::dsp
{
    RealFft::RealFft (int fftOrder)
        : order (juce::jlimit (4, 16, fftOrder)), size (1 << order), half (size / 2)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;

        twiddles.resize ((size_t) juce::jmax (1, half / 2));
        for (int k = 0; k < (int) twiddles.size(); ++k)
            twiddles[(size_t) k] = std::polar (1.0f, (float) (-twoPi * k / half));

        realTwiddles.resize ((size_t) half + 1);
        for (int k = 0; k <= half; ++k)
            realTwiddles[(size_t) k] = std::polar (1.0f, (float) (-twoPi * k / size));

        const int bits = order - 1;
        bitReverse.resize ((size_t) half);
        for (int i = 0; i < half; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if ((i >> b) & 1)
                    r |= 1 << (bits - 1 - b);
            bitReverse[(size_t) i] = r;
        }

        work.resize ((size_t) half);
    }

    void RealFft::complexFft (std::complex<float>* data, bool inverseDirection) const
    {
        for (int i = 0; i < half; ++i)
            if (const auto j = bitReverse[(size_t) i]; j > i)
                std::swap (data[i], data[j]);

        for (int len = 2; len <= half; len <<= 1)
        {
            const int step = half / len;
            const int h = len / 2;

            for (int start = 0; start < half; start += len)
            {
                for (int k = 0; k < h; ++k)
                {
                    auto w = twiddles[(size_t) (k * step)];
                    if (inverseDirection)
                        w = std::conj (w);

                    const auto a = data[start + k];
                    const auto b = data[start + k + h] * w;
                    data[start + k] = a + b;
                    data[start + k + h] = a - b;
                }
            }
        }
    }

    void RealFft::forward (const float* input, std::complex<float>* bins)
    {
        auto* z = work.data();
        for (int n = 0; n < half; ++n)
            z[n] = { input[2 * n], input[2 * n + 1] };

        complexFft (z, false);

        // Split the transform of the packed even/odd samples into the real transform.
        for (int k = 0; k <= half; ++k)
        {
            const auto zk = z[k == half ? 0 : k];
            const auto zm = std::conj (z[k == 0 ? 0 : half - k]);
            const auto even = (zk + zm) * 0.5f;
            const auto odd = (zk - zm) * std::complex<float> (0.0f, -0.5f);
            bins[k] = even + realTwiddles[(size_t) k] * odd;
        }
    }

    void RealFft::inverse (const std::complex<float>* bins, float* output)
    {
        auto* z = work.data();
        for (int k = 0; k < half; ++k)
        {
            const auto xk = bins[k];
            const auto xm = std::conj (bins[half - k]);
            const auto even = (xk + xm) * 0.5f;
            const auto odd = (xk - xm) * std::conj (realTwiddles[(size_t) k]) * 0.5f;
            z[k] = even + std::complex<float> (0.0f, 1.0f) * odd;
        }

        complexFft (z, true);

        const auto scale = 1.0f / (float) half;
        for (int n = 0; n < half; ++n)
        {
            output[2 * n] = z[n].real() * scale;
            output[2 * n + 1] = z[n].imag() * scale;
        }
    }

    std::vector<float> hannWindow (int size)
    {
        std::vector<float> w ((size_t) size);
        for (int i = 0; i < size; ++i)
            w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));

        return w;
    }
}
