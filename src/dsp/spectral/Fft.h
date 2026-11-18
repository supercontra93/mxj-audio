// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <vector>

namespace mxj::dsp
{
    /** Fast Fourier transform of real signals, 2^order points (order 4 ... 16).

        Our own small radix-2 implementation with precomputed tables, so the
        spectral tools need no extra library (master plan names PFFFT as an
        option; this is fast enough for now, see docs/adr/0008).

        A real signal of N samples has N/2 + 1 complex bins: bin k is k * rate / N Hz.
        forward() does not scale; inverse() divides by N, so inverse (forward (x)) == x.
        Allocates only in the constructor; forward() and inverse() are real-time safe.
        One object per thread (it has a work buffer).
    */
    class RealFft
    {
    public:
        explicit RealFft (int order);

        int getOrder() const noexcept   { return order; }
        int getSize() const noexcept    { return size; }
        int getNumBins() const noexcept { return size / 2 + 1; }

        /** `input`: getSize() samples. `bins`: getNumBins() values. */
        void forward (const float* input, std::complex<float>* bins);

        /** `bins`: getNumBins() values (bin 0 and the last bin must be real). `output`: getSize() samples. */
        void inverse (const std::complex<float>* bins, float* output);

    private:
        void complexFft (std::complex<float>* data, bool inverseDirection) const;

        int order, size, half;
        std::vector<std::complex<float>> twiddles;     ///< exp(-2 pi i k / half), k < half / 2
        std::vector<std::complex<float>> realTwiddles; ///< exp(-2 pi i k / size), k <= half
        std::vector<int> bitReverse;
        std::vector<std::complex<float>> work;
    };

    /** A periodic Hann window of `size` points (sums to a constant at hop size / 2 and / 4). */
    std::vector<float> hannWindow (int size);
}
